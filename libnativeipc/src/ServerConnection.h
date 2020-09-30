#pragma once

#include "ConnectionBase.h"
#include "IServerConnection.h"
#include "OperationQueue.h"
#include <unordered_map>

namespace Twitch::IPC {

class ServerConnection
    : public IServerConnection
    , public ConnectionBase {
public:
    ServerConnection(std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint, bool oneConnectionOnly=false, bool allowMultiuserAccess=false);
    ~ServerConnection();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ServerConnection);

    void connect() override;
    void disconnect() override;

    int activeConnections() override;

    void broadcast(Payload message) override;
    void send(Handle connectionHandle, Payload message) override;
    void invoke(Handle connectionHandle, Payload message, PromiseCallback onResult) override;
    Handle invoke(Handle connectionHandle, Payload message) override;
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

    std::unique_ptr<IServerTransport> _transport;
    std::unordered_map<Handle, std::unordered_map<Handle, PromiseCallback>> _callbacks;
    std::mutex _callbacksMutex;
    bool _latestConnectionOnly;
    bool _allowMultiuserAccess;

    OnDataHandler _receivedHandler;
    OnInvokedPromiseIdHandler _invokedPromiseIdHandler;
    OnInvokedImmediateHandler _invokedImmediateHandler;
    OnInvokedCallbackHandler _invokedCallbackHandler;
    OnResultHandler _resultHandler;
    OnHandler _connectHandler;
    OnHandler _disconnectHandler;
    OnHandler _errorHandler;
    OnLogHandler _logHandler;

    void handleError(Handle handle);
    void handleRemoteDisconnected(Handle handle);
    void handleRemoteConnected(Handle handle);
    void handleData(Handle connectionHandle, Handle handle, Payload message);
    void handleLog(
        Handle handle, LogLevel level, std::string message, std::string category = DefaultCategory);
};
} // namespace Twitch::IPC
