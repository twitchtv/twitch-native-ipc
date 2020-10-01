// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "ConnectionBase.h"
#include "IConnection.h"
#include "OperationQueue.h"
#include <unordered_map>

namespace Twitch::IPC {
class ClientConnection final
    : public IConnection
    , public ConnectionBase {
public:
    ClientConnection(std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint);
    ~ClientConnection();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ClientConnection);

    void connect() override;
    void disconnect() override;

    void send(Payload message) override;
    void invoke(Payload message, PromiseCallback onResult) override;
    Handle invoke(Payload message) override;
    void sendResult(Handle connectionHandle, Handle promiseId, Payload message) override;

    void onReceived(OnDataHandler dataHandler) override;
    void onInvoked(OnInvokedPromiseIdHandler dataHandler) override;
    void onInvoked(OnInvokedImmediateHandler dataHandler) override;
    void onInvoked(OnInvokedCallbackHandler dataHandler) override;
    void onResult(OnResultHandler dataHandler) override;
    void onConnect(OnHandler connectHandler) override;
    void onDisconnect(OnHandler disconnectHandler) override;
    void onError(OnHandler errorHandler) override;
    void onLog(OnLogHandler logHandler, LogLevel level) override;
    void setLogLevel(LogLevel level) override;

protected:
    LogLevel _logLevel = LogLevel::None;
    OperationQueue _outputQueue;

    std::unique_ptr<IClientTransport> _transport;
    std::unordered_map<Handle, PromiseCallback> _callbacks;
    std::mutex _callbacksMutex;

    OnDataHandler _receivedHandler;
    OnInvokedPromiseIdHandler _invokedPromiseIdHandler;
    OnInvokedImmediateHandler _invokedImmediateHandler;
    OnInvokedCallbackHandler _invokedCallbackHandler;
    OnResultHandler _resultHandler;
    OnHandler _connectHandler;
    OnHandler _disconnectHandler;
    OnHandler _errorHandler;
    OnLogHandler _logHandler;

    void handleError();
    void handleRemoteDisconnected();
    void handleRemoteConnected();
    void handleData(Handle connectionHandle, Handle promiseId, Payload message);
    void handleLog(Handle connectionHandle, LogLevel level, std::string message, std::string category = DefaultCategory);
};
} // namespace Twitch::IPC
