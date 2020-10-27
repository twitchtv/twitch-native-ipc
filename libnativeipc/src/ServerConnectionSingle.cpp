// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "ServerConnectionSingle.h"

using namespace Twitch::IPC;

ServerConnectionSingle::ServerConnectionSingle(std::shared_ptr<ConnectionFactory::Factory> factory,
    std::string endpoint,
    bool allowMultiuserAccess)
    : _connection(std::move(factory), std::move(endpoint), true, allowMultiuserAccess)
{
    _connection.onConnect([this](Handle connectionHandle) {
        _connectionHandle = connectionHandle;
    });
}

void ServerConnectionSingle::connect()
{
    _connection.connect();
}

void ServerConnectionSingle::disconnect()
{
    _connection.disconnect();
}

void ServerConnectionSingle::send(Payload message)
{
    if(_connectionHandle) {
        _connection.send(_connectionHandle, std::move(message));
    }
}

Handle ServerConnectionSingle::invoke(Payload message)
{
    if(_connectionHandle) {
        return _connection.invoke(_connectionHandle, message);
    }
    return 0;
}

void ServerConnectionSingle::invoke(Payload message, PromiseCallback onResult)
{
    if(_connectionHandle) {
        _connection.invoke(_connectionHandle, message, onResult);
    }
}

void ServerConnectionSingle::sendResult(Handle connectionHandle, Handle promiseId, Payload message)
{
    if(_connectionHandle && _connectionHandle == connectionHandle) {
        _connection.sendResult(_connectionHandle, promiseId, message);
    }
}

void ServerConnectionSingle::setLogLevel(LogLevel level)
{
    _connection.setLogLevel(level);
}

void ServerConnectionSingle::onReceived(OnDataHandler dataHandler)
{
    if(!dataHandler) {
        _connection.onReceived(nullptr);
    } else {
        _connection.onReceived([this, dataHandler](Handle connectionHandle, Payload message) {
            if(_connectionHandle && _connectionHandle == connectionHandle) {
                dataHandler(std::move(message));
            }
        });
    }
}

void ServerConnectionSingle::onInvoked(OnInvokedPromiseIdHandler dataHandler)
{
    if(!dataHandler) {
        _connection.onInvoked(static_cast<IServerConnection::OnInvokedCallbackHandler>(nullptr));
    } else {
        _connection.onInvoked(
            [this, dataHandler](Handle connectionHandle, Handle promiseId, Payload message) {
                if(_connectionHandle && _connectionHandle == connectionHandle) {
                    dataHandler(_connectionHandle, promiseId, std::move(message));
                }
            });
    }
}

void ServerConnectionSingle::onInvoked(OnInvokedImmediateHandler dataHandler)
{
    if(!dataHandler) {
        _connection.onInvoked(static_cast<IServerConnection::OnInvokedCallbackHandler>(nullptr));
    } else {
        _connection.onInvoked([this, dataHandler](Handle connectionHandle, Payload message) {
            if(_connectionHandle && _connectionHandle == connectionHandle) {
                return dataHandler(std::move(message));
            }
            return Payload();
        });
    }
}

void ServerConnectionSingle::onInvoked(OnInvokedCallbackHandler dataHandler)
{
    if(!dataHandler) {
        _connection.onInvoked(static_cast<IServerConnection::OnInvokedCallbackHandler>(nullptr));
    } else {
        _connection.onInvoked(
            [this, dataHandler](Handle connectionHandle, Payload message, ResultCallback callback) {
                if(_connectionHandle && _connectionHandle == connectionHandle) {
                    dataHandler(std::move(message), std::move(callback));
                }
            });
    }
}

void ServerConnectionSingle::onResult(OnResultHandler dataHandler)
{
    if(!dataHandler) {
        _connection.onResult(nullptr);
    } else {
        _connection.onResult(
            [this, dataHandler](Handle connectionHandle, Handle promiseId, Payload message) {
                if(_connectionHandle && _connectionHandle == connectionHandle) {
                    dataHandler(promiseId, std::move(message));
                }
            });
    }
}

void ServerConnectionSingle::onConnect(OnHandler handler)
{
    if(!handler) {
        _connection.onConnect([this](Handle connectionHandle) {
            _connectionHandle = connectionHandle;
        });
    } else {
        _connection.onConnect([this, handler](Handle connectionHandle) {
            _connectionHandle = connectionHandle;
            handler();
        });
    }
}

void ServerConnectionSingle::onDisconnect(OnHandler handler)
{
    if(!handler) {
        _connection.onDisconnect(nullptr);
    } else {
        _connection.onDisconnect([handler](Handle) { handler(); });
    }
}

void ServerConnectionSingle::onError(OnHandler handler)
{
    if(!handler) {
        _connection.onError(nullptr);
    } else {
        _connection.onError([handler](Handle) { handler(); });
    }
}

void ServerConnectionSingle::onLog(OnLogHandler handler, LogLevel level)
{
    if(!handler) {
        _connection.onLog(nullptr, level);
    } else {
        _connection.onLog(
            [handler](Handle, LogLevel level, std::string message, std::string category) {
                handler(level, std::move(message), std::move(category));
            },
            level);
    }
}
