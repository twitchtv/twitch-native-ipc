// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ConnectionExports.h"

namespace Twitch::IPC {

using Handle = uint32_t;
enum class LogLevel { Debug, Info, Warning, Error, None };
enum class InvokeResultCode { Good, RemoteDisconnect, LocalDisconnect };

NATIVEIPC_LIBSPEC LogLevel fromString(const char *value);
NATIVEIPC_LIBSPEC const char *toString(LogLevel value);

struct Payload : std::vector<uint8_t> {
    Payload() = default;
    // ReSharper disable CppNonExplicitConvertingConstructor
    Payload(std::vector<uint8_t> &&vec)
        : std::vector<uint8_t>(std::move(vec))
    {
    }
    Payload(const std::vector<uint8_t> &vec)
        : std::vector<uint8_t>(vec)
    {
    }
    Payload(const uint8_t *ptr, int length)
        : std::vector<uint8_t>(ptr, ptr + length)
    {
    }
    Payload(const uint8_t *ptr, size_t length)
        : std::vector<uint8_t>(ptr, ptr + length)
    {
    }
    Payload(const std::string &message)
        : std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(message.data()),
              reinterpret_cast<const uint8_t *>(message.data()) + message.length())
    {
    }
    Payload(const char *message)
        : std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(message),
              reinterpret_cast<const uint8_t *>(message) + strlen(message))
    {
    }
    // ReSharper restore CppNonExplicitConvertingConstructor

    [[nodiscard]] std::string asString() const
    {
        return std::string(reinterpret_cast<const char *>(data()), size());
    }
};

class IConnection {
public:
    using PromiseCallback = std::function<void(InvokeResultCode resultCode, Payload result)>;
    using ResultCallback = std::function<void(Payload result)>;
    using OnHandler = std::function<void()>;
    using OnDataHandler = std::function<void(Payload message)>;
    using OnInvokedPromiseIdHandler = std::function<void(Handle connectionHandle, Handle promiseId, Payload message)>;
    using OnInvokedImmediateHandler = std::function<Payload(Payload message)>;
    using OnInvokedCallbackHandler = std::function<void(Payload message, ResultCallback callback)>;
    using OnResultHandler = std::function<void(Handle promiseId, Payload message)>;
    using OnLogHandler =
        std::function<void(LogLevel level, std::string message, std::string category)>;

    IConnection() = default;
    virtual ~IConnection() = default;

    IConnection(const IConnection &) = delete;
    IConnection(IConnection &&) = delete;
    IConnection &operator=(const IConnection &) = delete;
    IConnection &operator=(IConnection &&) = delete;

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual void send(Payload message) = 0;
    virtual void invoke(Payload message, PromiseCallback onResult) = 0;
    virtual Handle invoke(Payload message) = 0;
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
