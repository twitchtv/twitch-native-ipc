#pragma once

#include "IServerTransport.h"
#include "UVTransportBase.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Twitch::IPC {
class UVServerTransport
    : public IServerTransport
    , public UVTransportBase {
public:
    UVServerTransport(bool latestConnectionOnly, bool allowMultiuserAccess);
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(UVServerTransport);

    bool listen(std::string endpoint) override;
    void send(Handle connectionHandle, Handle promiseId, Payload message) override;
    void broadcast(Payload message) override;
    void setLogLevel(LogLevel level) override;
    int activeConnections() override;

    void onConnect(OnHandler handler) override;
    void onDisconnect(OnHandler handler) override;
    void onData(OnDataHandler handler) override;
    void onNoInvokeClientHandler(OnNoInvokeClientHandler) override;
    void onLog(OnLogHandler handler, LogLevel level) override;

protected:
    void destroy();

    enum class Status {
        Disconnected,
        Listening,
        ListenFailed,
        Disconnecting,
    };

    std::mutex _clientMutex;
    std::unordered_map<uv_stream_t *, std::shared_ptr<ClientInfo>> _clientsByStream;
    bool _latestConnectionOnly;
    bool _allowMultiuserAccess;

    Status _status{Status::Disconnected};

    void handleConnected(uv_stream_t *stream, int status) override;
    void handleDisconnected(uv_stream_t *stream) override;
    void handleWrite(uv_stream_t *stream, int status) override;

    ClientInfo *getClientInfo(uv_stream_t *stream) override;
    ClientInfo *getClientInfo(Handle connectionHandle) override;
    void shutdownClients();
    bool isDisconnecting(const std::unique_lock<std::mutex>&) override;
    bool isConnected(const std::unique_lock<std::mutex>&) override;

    static std::string statusName(Status status);
    void setStatus(Status value);
    void runLoopThread();

    virtual int acceptClient(uv_stream_t *stream, uv_stream_t *&) = 0;
    virtual int bind() = 0;
    virtual int startListening() = 0;
    virtual void closeBinder() = 0;
};
} // namespace Twitch::IPC
