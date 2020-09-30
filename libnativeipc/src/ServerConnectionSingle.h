#pragma once

#include "IConnection.h"
#include "ServerConnection.h"

namespace Twitch::IPC {
class ServerConnectionSingle : public IConnection {
public:
    ServerConnectionSingle(
        std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint, bool allowMultiuserAccess=false);
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ServerConnectionSingle);

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
    Handle _connectionHandle{};
    ServerConnection _connection;
};
} // namespace Twitch::IPC
