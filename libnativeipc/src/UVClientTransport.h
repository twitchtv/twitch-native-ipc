// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "IClientTransport.h"
#include "UVTransportBase.h"
#include <thread>

namespace Twitch::IPC {
class UVClientTransport
    : public IClientTransport
    , public UVTransportBase {
public:
    UVClientTransport();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(UVClientTransport);

    ConnectResult connect(std::string endpoint) override;
    void send(Handle connectionHandle, Handle promiseId, Payload message) override;
    void setLogLevel(LogLevel level) override;

    void onConnect(OnHandler handler) override;
    void onDisconnect(OnHandler handler) override;
    void onData(OnDataHandler handler) override;
    void onError(OnHandler errorHandler) override;
    void onLog(OnLogHandler logHandler, LogLevel level) override;

protected:
    void destroy();

    enum class Status {
        Disconnected,
        Connecting,
        Connected,
        WriteFailed,
        Disconnecting,
    };

    std::unique_ptr<ClientInfo> _clientInfo;
    std::unique_ptr<uv_stream_t> _socket;
    uv_connect_t _connect{};
    uv_timer_t _retry{};
    int _retryDelay{};
    bool _retrying{};

    Status _status{Status::Disconnected};

    static void timer_cb(uv_timer_t *handle);
    static void connect_cb(uv_connect_t *req, int status);

    void handleConnected(uv_stream_t *stream, int status) override;
    void handleTimer();
    void handleWrite(uv_stream_t *handle, int status) override;
    void handleDisconnected(uv_stream_t *stream) override;
    void doDisconnectCleanup(const std::unique_lock<std::mutex>&) override;

    ClientInfo *getClientInfo(uv_stream_t *stream) override;
    ClientInfo *getClientInfo(Handle connectionHandle) override;
    bool isDisconnecting(const std::unique_lock<std::mutex>&) override;
    bool isConnected(const std::unique_lock<std::mutex>&) override;

    void closeSocket();
    void setStatus(Status value);
    void runLoopThread();
    static std::string statusName(Status status);

    virtual bool connectSocket() = 0;
};
} // namespace Twitch::IPC
