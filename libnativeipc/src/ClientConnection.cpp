#include "ClientConnection.h"
#include "IClientTransport.h"
#include "LogMacrosNoHandle.h"
#include <string>

using namespace Twitch::IPC;

ClientConnection::ClientConnection(
    std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint)
    : ConnectionBase(std::move(factory), std::move(endpoint))
{
}

ClientConnection::~ClientConnection()
{
    clearLambdaShield();
    {
        std::lock_guard guard(_transportMutex);
        _shuttingDown = true;
    }
    _outputQueue.stop();
    _transport.reset();
}

void ClientConnection::connect()
{
    LOG_INFO("`connect`");

    if (_endpoint.empty()) {
        LOG_ERROR("No endpoint specified.");
        return;
    }

    std::lock_guard guard(_transportMutex);
    if(_transport) {
        LOG_DEBUG("`connect` called but already connected");
        return;
    }

    if (_shuttingDown) {
        LOG_DEBUG("`connect` called but already shutting down");
        return;
    }

    _transport = _factory->makeClientProtocol();
    _transport->onData([this](Handle connectionHandle, Handle promiseId, Payload data) {
        handleData(connectionHandle, promiseId, std::move(data));
    });

    _transport->onDisconnect([this](Handle) {
        LOG_INFO("`onDisconnect` called");
        handleRemoteDisconnected();
    });

    _transport->onConnect([this](Handle) {
        LOG_INFO("`onConnect` called");
        handleRemoteConnected();
    });
    _transport->onError([this](Handle) {
        LOG_ERROR("Got onError callback");
        handleError();
    });
    _transport->onLog(
        [this](Handle connectionHandle, LogLevel level, std::string message) {
            handleLog(connectionHandle, level, std::move(message), "transport");
        },
        _logLevel);

    const auto status = _transport->connect(_endpoint);
    switch(status) {
    case ConnectResult::Connected:
        LOG_INFO("Connected immediately");
        break;
    case ConnectResult::Connecting:
        LOG_INFO("Waiting to connect");
        break;
    case ConnectResult::ShuttingDown:
        LOG_INFO("Connect cancelled.");
        break;
    case ConnectResult::Failed:
        LOG_WARNING("Connect failed.");
        break;
    }
    if (status != ConnectResult::Connected && status != ConnectResult::Connecting) {
        _transport.reset();
    }
}

void ClientConnection::disconnect()
{
    LOG_INFO("`disconnect`");
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
    for (auto &cb: callbacks) {
        cb.second(InvokeResultCode::LocalDisconnect, {});
    }
}

void ClientConnection::send(Payload message)
{
    LOG_DEBUG("Sending message of length " + std::to_string(message.size()));
    std::lock_guard guard(_transportMutex);
    if(_transport && !_shuttingDown) {
        _transport->send(0, 0, std::move(message));
    }
}

Handle ClientConnection::invoke(Payload message)
{
    const auto handle = getNextHandle();
    LOG_DEBUG("Sending invoke of length " + std::to_string(message.size()));
    std::lock_guard guard(_transportMutex);
    if(_transport && !_shuttingDown) {
        _transport->send(0, handle, std::move(message));
    }
    return handle;
}

void ClientConnection::invoke(Payload message, PromiseCallback onResult)
{
    LOG_DEBUG("Sending invoke of length " + std::to_string(message.size()));
    const auto handle = getNextHandle();
    std::unique_lock guard(_transportMutex);
    if(_transport && !_shuttingDown) {
        {
            std::lock_guard callbackGuard(_callbacksMutex);
            _callbacks[handle] = std::move(onResult);
        }
        _transport->send(0, handle, std::move(message));
    } else if (!_shuttingDown) {
        guard.unlock();
        onResult(InvokeResultCode::LocalDisconnect, {});
    }
}

void ClientConnection::sendResult(Handle connectionHandle, Handle promiseId, Payload message)
{
    LOG_DEBUG("Sending invoke result " + std::to_string(promiseId) + " of length " +
              std::to_string(message.size()));
    std::lock_guard guard(_transportMutex);
    if(_transport && !_shuttingDown) {
        _transport->send(connectionHandle, promiseId | ResponseFlag, std::move(message));
    }
}

void ClientConnection::handleRemoteConnected()
{
    _outputQueue.enqueue([this] {
        if(_connectHandler) {
            _connectHandler();
        }
    });
}

void ClientConnection::handleRemoteDisconnected()
{
    // Remove any lingering invoke callbacks for this connection
    std::unordered_map<Handle, PromiseCallback> expiredInvokeCallbacks;
    {
        std::unique_lock guard(_callbacksMutex);
        expiredInvokeCallbacks.swap(_callbacks);
    }
    _outputQueue.enqueue([this, expiredInvokeCallbacks = std::move(expiredInvokeCallbacks)] {
        for (auto &cb: expiredInvokeCallbacks) {
            cb.second(InvokeResultCode::RemoteDisconnect, {});
        }
        if(_disconnectHandler) {
            _disconnectHandler();
        }
    });
}

void ClientConnection::handleData(Handle connectionHandle, Handle handle, Payload message)
{
    _outputQueue.enqueue([this, connectionHandle, handle, message = std::move(message)]() mutable {
        if(!handle) {
            if(_receivedHandler) {
                _receivedHandler(std::move(message));
            }
        } else if(handle & ResponseFlag) {
            const auto promiseId = handle & ~ResponseFlag;
            {
                std::unique_lock guard(_callbacksMutex);
                auto i = _callbacks.find(promiseId);
                if(i != _callbacks.end()) {
                    const auto cb = std::move(i->second);
                    _callbacks.erase(i);
                    guard.unlock();
                    LOG_DEBUG("Processing invoke result " + std::to_string(promiseId) +
                              " of length " + std::to_string(message.size()));
                    cb(InvokeResultCode::Good, std::move(message));
                    return;
                }
            }
            if(_resultHandler) {
                LOG_DEBUG("Processing invoke result " + std::to_string(promiseId) + " of length " +
                          std::to_string(message.size()) + "with global handler");
                _resultHandler(promiseId, std::move(message));
            } else {
                LOG_DEBUG("Could not process invoke result " + std::to_string(promiseId));
            }
            // handle invoke
        } else if(_transport) {
            auto promiseId = handle;
            LOG_DEBUG("Received invoke request " + std::to_string(promiseId) + " of length " +
                      std::to_string(message.size()));
            if(_invokedPromiseIdHandler) {
                _invokedPromiseIdHandler(connectionHandle, promiseId, std::move(message));
            } else if(_invokedImmediateHandler) {
                auto result = _invokedImmediateHandler(std::move(message));
                LOG_DEBUG("Sending invoke result " + std::to_string(promiseId) + " of length " +
                          std::to_string(result.size()));
                std::lock_guard guard(_transportMutex);
                if(_transport && !_shuttingDown) {
                    _transport->send(connectionHandle, promiseId | ResponseFlag, std::move(result));
                }
            } else if(_invokedCallbackHandler) {
                _invokedCallbackHandler(std::move(message), [this, connectionHandle, promiseId, shieldLocker = std::weak_ptr<int>(_lambdaShield)](Payload result) {
                    auto lock = shieldLocker.lock();
                    if (lock) {
                        LOG_DEBUG("Sending invoke result " + std::to_string(promiseId) + " of length " +
                                  std::to_string(result.size()));
                        std::lock_guard guard(_transportMutex);
                        if (_transport && !_shuttingDown) {
                            _transport->send(connectionHandle, promiseId | ResponseFlag, std::move(result));
                        }
                    }
                });
            }
        }
    });
}

void ClientConnection::handleError()
{
    _outputQueue.enqueue([this] {
        if(_errorHandler) {
            _errorHandler();
        }
    });
}

void ClientConnection::handleLog(Handle, LogLevel level, std::string message, std::string category)
{
    if(_logHandler && level >= _logLevel) {
        _outputQueue.enqueue(
            [this, level, message = std::move(message), category = std::move(category)]() {
                // check again just in case this changed since we were enqueued
                if(_logHandler && static_cast<int>(level) >= static_cast<int>(_logLevel)) {
                    _logHandler(level, message, category);
                }
            });
    }
}

void ClientConnection::onReceived(OnDataHandler dataHandler)
{
    _receivedHandler = dataHandler;
}

void ClientConnection::onInvoked(OnInvokedPromiseIdHandler dataHandler)
{
    _invokedPromiseIdHandler = dataHandler;
    _invokedImmediateHandler = nullptr;
    _invokedCallbackHandler = nullptr;
}

void ClientConnection::onInvoked(OnInvokedImmediateHandler dataHandler)
{
    _invokedPromiseIdHandler = nullptr;
    _invokedImmediateHandler = dataHandler;
    _invokedCallbackHandler = nullptr;
}

void ClientConnection::onInvoked(OnInvokedCallbackHandler dataHandler)
{
    _invokedPromiseIdHandler = nullptr;
    _invokedImmediateHandler = nullptr;
    _invokedCallbackHandler = dataHandler;
}

void ClientConnection::onResult(OnResultHandler dataHandler)
{
    _resultHandler = dataHandler;
}

void ClientConnection::onConnect(OnHandler connectHandler)
{
    _connectHandler = connectHandler;
}

void ClientConnection::onDisconnect(OnHandler disconnectHandler)
{
    _disconnectHandler = disconnectHandler;
}

void ClientConnection::onError(OnHandler errorHandler)
{
    _errorHandler = errorHandler;
}

void ClientConnection::onLog(OnLogHandler logHandler, LogLevel level)
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

void ClientConnection::setLogLevel(LogLevel level)
{
    _logLevel = level;
    if(_transport) {
        _transport->setLogLevel(level);
    }
}
