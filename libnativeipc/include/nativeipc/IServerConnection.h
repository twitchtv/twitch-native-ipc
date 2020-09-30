#pragma once

#include "IConnection.h"

namespace Twitch::IPC {
class IServerConnection {
public:
    using PromiseCallback = IConnection::PromiseCallback;
    using ResultCallback = IConnection::ResultCallback;
    using OnHandler = std::function<void(Handle connectionHandle)>;
    using OnDataHandler = std::function<void(Handle connectionHandle, Payload data)>;
    using OnInvokedPromiseIdHandler =
        std::function<void(Handle connectionHandle, Handle promiseId, Payload message)>;
    using OnInvokedImmediateHandler =
        std::function<Payload(Handle connectionHandle, Payload message)>;
    using OnInvokedCallbackHandler =
        std::function<void(Handle connectionHandle, Payload message, ResultCallback callback)>;
    using OnResultHandler =
        std::function<void(Handle connectionHandle, Handle promiseId, Payload data)>;
    using OnLogHandler = std::function<void(
        Handle connectionHandle, LogLevel level, std::string message, std::string category)>;

    IServerConnection() = default;
    virtual ~IServerConnection() = default;

    IServerConnection(const IServerConnection &) = delete;
    IServerConnection(IServerConnection &&) = delete;
    IServerConnection &operator=(const IServerConnection &) = delete;
    IServerConnection &operator=(IServerConnection &&) = delete;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual int activeConnections() = 0;

    virtual void broadcast(Payload message) = 0;
    virtual void send(Handle connectionHandle, Payload message) = 0;
    virtual void invoke(Handle connectionHandle, Payload message, PromiseCallback onResult) = 0;
    virtual Handle invoke(Handle connectionHandle, Payload message) = 0;
    virtual void sendResult(Handle connectionHandle, Handle promiseId, Payload message) = 0;

    virtual void onReceived(OnDataHandler dataHandler) = 0;
    virtual void onInvoked(OnInvokedPromiseIdHandler dataHandler) = 0;
    virtual void onInvoked(OnInvokedImmediateHandler dataHandler) = 0;
    virtual void onInvoked(OnInvokedCallbackHandler dataHandler) = 0;
    virtual void onResult(OnResultHandler dataHandler) = 0;
    virtual void onConnect(OnHandler connectHandler) = 0;
    virtual void onDisconnect(OnHandler disconnectHandler) = 0;
    virtual void onError(OnHandler errorHandler) = 0;
    virtual void onLog(OnLogHandler logHandler, LogLevel level = LogLevel::None) = 0;
    virtual void setLogLevel(LogLevel level) = 0;
};
} // namespace Twitch::IPC
