#include "ServerConnection.h"
#include "ClientConnection.h"
#include "LogMacrosWithHandle.h"

using namespace Twitch::IPC;

ServerConnection::ServerConnection(std::shared_ptr<ConnectionFactory::Factory> factory,
    std::string endpoint,
    bool oneConnectionOnly,
    bool allowMultiuserAccess)
    : ConnectionBase(std::move(factory), std::move(endpoint))
    , _latestConnectionOnly(oneConnectionOnly)
    , _allowMultiuserAccess(allowMultiuserAccess)
{
}

ServerConnection::~ServerConnection()
{
    clearLambdaShield();
    {
        std::lock_guard guard(_transportMutex);
        _shuttingDown = true;
    }
    _outputQueue.stop();
    _transport.reset();
}

void ServerConnection::connect()
{
    LOG_INFO(0, "`connect`");

    if (_endpoint.empty()) {
        LOG_ERROR(0, "No endpoint specified.");
        return;
    }

    std::lock_guard guard(_transportMutex);
    if(_transport) {
        LOG_DEBUG(0, "`connect` called but already connected");
        return;
    }

    if (_shuttingDown) {
        LOG_DEBUG(0, "`connect` called but already shutting down");
        return;
    }

    _transport = _factory->makeServerProtocol(_latestConnectionOnly, _allowMultiuserAccess);
    _transport->onData([this](Handle connectionHandle, Handle handle, Payload data) {
        handleData(connectionHandle, handle, std::move(data));
    });
    _transport->onNoInvokeClientHandler([this](Handle connectionHandle, Handle promiseId) {
        std::unique_lock guard(_callbacksMutex);
        auto i = _callbacks.find(connectionHandle);
        if(i != _callbacks.end()) {
            auto &callbacks = i->second;
            auto j = callbacks.find(promiseId);
            if (j != callbacks.end()) {
                auto cb = std::move(j->second);
                callbacks.erase(j);
                guard.unlock();
                LOG_DEBUG(connectionHandle, "Rejecting invoke for missing client");
                cb(InvokeResultCode::RemoteDisconnect, {});
            }
        }
    });
    _transport->onConnect([this](Handle connectionHandle) {
        LOG_INFO(connectionHandle, "`onConnect` called");
        handleRemoteConnected(connectionHandle);
    });
    _transport->onDisconnect([this](Handle connectionHandle) {
        LOG_INFO(connectionHandle, "`onDisconnect` called");
        handleRemoteDisconnected(connectionHandle);
    });
    _transport->onLog(
        [this](Handle handle, LogLevel level, std::string message) {
            handleLog(handle, level, std::move(message), "transport");
        },
        _logLevel);
    if(!_transport->listen(_endpoint)) {
        LOG_ERROR(0, "Failed to start server");
        _transport.reset();
        handleError(0);
    }
}

void ServerConnection::disconnect()
{
    LOG_INFO(0, "`disconnect`");
    std::unique_lock guard(_transportMutex);
    if (_shuttingDown) {
        return;
    }
    _transport.reset();
    
    decltype(_callbacks) callbacks;
    {
        std::lock_guard callbackGuard(_callbacksMutex);
        callbacks.swap(_callbacks);
    }
    guard.unlock();
    for (auto &client_callback : callbacks) {
        for (auto &cb : client_callback.second) {
            cb.second(InvokeResultCode::LocalDisconnect, {});
        }
    }
}

int ServerConnection::activeConnections()
{
    std::lock_guard guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        return _transport->activeConnections();
    }
    return 0;
}

void ServerConnection::broadcast(Payload message)
{
    std::lock_guard guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        _transport->broadcast(std::move(message));
    }
}

void ServerConnection::send(Handle connectionHandle, Payload message)
{
    LOG_DEBUG(connectionHandle, "Sending message of length " + std::to_string(message.size()));
    std::lock_guard guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        _transport->send(connectionHandle, 0, std::move(message));
    }
}

Handle ServerConnection::invoke(Handle connectionHandle, Payload message)
{
    LOG_DEBUG(connectionHandle, "Sending invoke of length " + std::to_string(message.size()));
    const auto promiseId = getNextHandle();
    std::lock_guard guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        _transport->send(connectionHandle, promiseId, std::move(message));
    }
    return promiseId;
}

void ServerConnection::invoke(Handle connectionHandle, Payload message, PromiseCallback onResult)
{
    LOG_DEBUG(connectionHandle, "Sending invoke of length " + std::to_string(message.size()));
    const auto promiseId = getNextHandle();
    std::unique_lock guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        {
            std::lock_guard callbackGuard(_callbacksMutex);
            _callbacks[connectionHandle][promiseId] = std::move(onResult);
        }
        _transport->send(connectionHandle, promiseId, std::move(message));
    } else if (!_shuttingDown) {
        guard.unlock();
        onResult(InvokeResultCode::LocalDisconnect, {});
    }
}

void ServerConnection::sendResult(Handle connectionHandle, Handle promiseId, Payload message)
{
    LOG_DEBUG(connectionHandle, "Sending invoke result of length " + std::to_string(message.size()));
    std::lock_guard guard(_transportMutex);
    if (_transport && !_shuttingDown) {
        _transport->send(connectionHandle, promiseId | ResponseFlag, std::move(message));
    }
}

void ServerConnection::handleRemoteConnected(Handle handle)
{
    _outputQueue.enqueue([this, handle] {
        if(_connectHandler) {
            _connectHandler(handle);
        }
    });
}

void ServerConnection::handleRemoteDisconnected(Handle handle)
{
    // Remove any lingering invoke callbacks for this connection
    std::unordered_map<Handle, PromiseCallback> expiredInvokeCallbacks;
    {
        std::unique_lock guard(_callbacksMutex);
        auto callbacks = _callbacks.find(handle);
        if (callbacks != _callbacks.end()) {
            expiredInvokeCallbacks.swap(callbacks->second);
            _callbacks.erase(callbacks);
        }
    }
    _outputQueue.enqueue([this, handle, expiredInvokeCallbacks = std::move(expiredInvokeCallbacks)] {
        for (auto &cb : expiredInvokeCallbacks) {
            cb.second(InvokeResultCode::RemoteDisconnect, {});
        }
        if(_disconnectHandler) {
            _disconnectHandler(handle);
        }
    });
}

void ServerConnection::handleData(Handle connectionHandle, Handle handle, Payload message)
{
    _outputQueue.enqueue([this, connectionHandle, handle, message = std::move(message)]() mutable {
        if(!handle) {
            if(_receivedHandler) {
                _receivedHandler(connectionHandle, std::move(message));
            }
        } else if(handle & ResponseFlag) {
            const auto promiseId = handle & ~ResponseFlag;
            {
                std::unique_lock guard(_callbacksMutex);
                auto &callbacks = _callbacks[connectionHandle];
                auto i = callbacks.find(promiseId);
                if(i != callbacks.end()) {
                    auto cb = std::move(i->second);
                    callbacks.erase(i);
                    guard.unlock();
                    LOG_DEBUG(connectionHandle,
                        "Processing invoke result " + std::to_string(promiseId) + " of length " +
                            std::to_string(message.size()));
                    cb(InvokeResultCode::Good, std::move(message));
                    return;
                }
            }
            if(_resultHandler) {
                LOG_DEBUG(connectionHandle,
                    "Processing invoke result " + std::to_string(promiseId) + " of length " +
                        std::to_string(message.size()) + "with global handler");
                _resultHandler(connectionHandle, promiseId, std::move(message));
            } else {
                LOG_DEBUG(connectionHandle,
                    "Could not process invoke result " + std::to_string(promiseId));
            }
        } else if(_invokedPromiseIdHandler || _invokedImmediateHandler || _invokedCallbackHandler) {
            auto promiseId = handle;
            LOG_DEBUG(connectionHandle,
                "Received invoke request " + std::to_string(promiseId) + " of length " +
                    std::to_string(message.size()));
            if(_invokedPromiseIdHandler) {
                _invokedPromiseIdHandler(connectionHandle, promiseId, std::move(message));
            } else if(_invokedImmediateHandler) {
                auto result = _invokedImmediateHandler(connectionHandle, std::move(message));
                LOG_DEBUG(connectionHandle,
                    "Sending invoke result " + std::to_string(promiseId) + " of length " +
                        std::to_string(result.size()));
                std::lock_guard guard(_transportMutex);
                if (_transport && !_shuttingDown) {
                    _transport->send(connectionHandle, promiseId | ResponseFlag, std::move(result));
                }
            } else {
                _invokedCallbackHandler(connectionHandle,
                    std::move(message),
                    [this, connectionHandle, promiseId, shieldLocker = std::weak_ptr<int>(_lambdaShield)](Payload result) {
                        auto lock = shieldLocker.lock();
                        if (lock) {
                            LOG_DEBUG(connectionHandle,
                                      "Sending invoke result " + std::to_string(promiseId) + " of length " +
                                      std::to_string(result.size()));
                            std::lock_guard guard(_transportMutex);
                            if (_transport && !_shuttingDown) {
                                _transport->send(
                                        connectionHandle, promiseId | ResponseFlag, std::move(result));
                            }
                        }
                    });
            }
        }
    });
}

void ServerConnection::handleError(Handle handle)
{
    _outputQueue.enqueue([this, handle] {
        if(_errorHandler) {
            _errorHandler(handle);
        }
    });
}

void ServerConnection::handleLog(
    Handle handle, LogLevel level, std::string message, std::string category)
{
    if(_logHandler && level >= _logLevel) {
        _outputQueue.enqueue(
            [this, handle, level, message = std::move(message), category = std::move(category)]() {
                // check again just in case this changed since we were enqueued
                if(_logHandler && static_cast<int>(level) >= static_cast<int>(_logLevel)) {
                    _logHandler(handle, level, message, category);
                }
            });
    }
}

void ServerConnection::onReceived(OnDataHandler dataHandler)
{
    _receivedHandler = dataHandler;
}

void ServerConnection::onInvoked(OnInvokedPromiseIdHandler dataHandler)
{
    _invokedPromiseIdHandler = dataHandler;
    _invokedImmediateHandler = nullptr;
    _invokedCallbackHandler = nullptr;
}

void ServerConnection::onInvoked(OnInvokedImmediateHandler dataHandler)
{
    _invokedPromiseIdHandler = nullptr;
    _invokedImmediateHandler = dataHandler;
    _invokedCallbackHandler = nullptr;
}

void ServerConnection::onInvoked(OnInvokedCallbackHandler dataHandler)
{
    _invokedPromiseIdHandler = nullptr;
    _invokedImmediateHandler = nullptr;
    _invokedCallbackHandler = dataHandler;
}

void ServerConnection::onResult(OnResultHandler dataHandler)
{
    _resultHandler = dataHandler;
}

void ServerConnection::onConnect(OnHandler connectHandler)
{
    _connectHandler = connectHandler;
}

void ServerConnection::onDisconnect(OnHandler disconnectHandler)
{
    _disconnectHandler = disconnectHandler;
}

void ServerConnection::onError(OnHandler errorHandler)
{
    _errorHandler = errorHandler;
}

void ServerConnection::onLog(OnLogHandler logHandler, LogLevel level)
{
    // Set _logLevel if level is anything but None
    // If level is None and _logLevel is None, change to Warning since otherwise hooking up a log handler is pretty useless
    if(level != LogLevel::None) {
        _logLevel = level;
    } else if(_logLevel == LogLevel::None) {
        _logLevel = LogLevel::Warning;
    }
    _logHandler = logHandler;
}

void ServerConnection::setLogLevel(LogLevel level)
{
    _logLevel = level;
    if(_transport) {
        _transport->setLogLevel(level);
    }
}
