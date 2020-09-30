#pragma once

#include "DeleteConstructors.h"
#include "IConnection.h"
#include <functional>

namespace Twitch::IPC {
class ITransportBase {
public:
    ITransportBase() = default;
    virtual ~ITransportBase() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ITransportBase);

    virtual void setLogLevel(LogLevel level) = 0;

    using OnHandler = std::function<void(Handle connectionHandle)>;
    using OnDataHandler =
        std::function<void(Handle connectionHandle, Handle requestHandle, Payload data)>;
    using OnLogHandler =
        std::function<void(Handle connectionHandle, LogLevel level, std::string message)>;
    using OnNoInvokeClientHandler = std::function<void(Handle connectionHandle, Handle promiseId)>;

    virtual void onConnect(OnHandler) = 0;
    virtual void onDisconnect(OnHandler) = 0;
    virtual void onData(OnDataHandler) = 0;
    virtual void onNoInvokeClientHandler(OnNoInvokeClientHandler) {}
    virtual void onError(OnHandler) {}
    virtual void onLog(OnLogHandler, LogLevel) = 0;
};
} // namespace Twitch::IPC
