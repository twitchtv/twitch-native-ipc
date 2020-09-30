#pragma once

#include <atomic>

#include "ITransportBase.h"
#include "Message.h"

#include <uv.h>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Twitch::IPC {

struct WriteRequest {
    uv_write_t req{};
    uv_buf_t buf;
    std::vector<uint8_t> data;
    explicit WriteRequest(std::vector<uint8_t> &&data)
        : buf(uv_buf_init(
              reinterpret_cast<char *>(data.data()), static_cast<unsigned>(data.size())))
        , data(std::move(data))
    {
    }
};

class UVTransportBase {
public:
    UVTransportBase();
    virtual ~UVTransportBase() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(UVTransportBase);

protected:
    void closeLoop();

    struct ClientInfo {
        uv_stream_t *stream;
        const Handle handle;
        MessageHeader messageHeader{};
        std::vector<uint8_t> messageBuffer;
        std::vector<char> receiveBuffer;

        explicit ClientInfo(uv_stream_t *s, const Handle h)
            : stream(s)
            , handle(h)
        {
        }
        void readToMessageBuffer(const uint8_t *&curPtr, const uint8_t *endPtr, int finalLength);
    };

    ClientInfo *getClientInfo(uv_handle_t *handle)
    {
        return getClientInfo(reinterpret_cast<uv_stream_t *>(handle));
    }
    virtual ClientInfo *getClientInfo(uv_stream_t *stream) = 0;
    virtual ClientInfo *getClientInfo(Handle connectionHandle) = 0;
    virtual bool isDisconnecting(const std::unique_lock<std::mutex>&) = 0;
    virtual bool isConnected(const std::unique_lock<std::mutex>&) = 0;
    virtual void handleConnected(uv_stream_t *stream, int status) = 0;
    virtual void handleWrite(uv_stream_t *stream, int status) = 0;
    virtual void handleDisconnected(uv_stream_t *stream) = 0;
    virtual void doDisconnectCleanup(const std::unique_lock<std::mutex>&) {}

    static void connection_cb(uv_stream_t *stream, int status);
    static void alloc_cb(uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf);
    static void read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

    void handleAlloc(uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf);
    void handleRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    void handleStateChanged();
    void handleLog(Handle handle, LogLevel level, std::string message);

    using WritePair = std::pair<Handle, std::unique_ptr<WriteRequest>>;
    void initSemaphore();
    void waitForSemaphore();
    void postSemaphore();

    void initStateChanged();
    void sendStateChanged(const std::lock_guard<std::mutex> &);
    void closeStateChanged(const std::lock_guard<std::mutex> &);

    void addToWriteQueue(Handle connectionHandle, Handle promiseId, Payload &&message);
    void disconnectStream(uv_stream_t *stream, bool shutdown);
    Handle getNextConnectionHandle();

    ITransportBase::OnHandler _connectHandler;
    ITransportBase::OnHandler _disconnectHandler;
    ITransportBase::OnDataHandler _dataHandler;
    ITransportBase::OnNoInvokeClientHandler _noInvokeClientHandler;
    ITransportBase::OnHandler _errorHandler;
    ITransportBase::OnLogHandler _logHandler;

    uv_loop_t _loop{};

    std::thread _thread;
    std::mutex _mutex;
    std::string _endpoint;

    LogLevel _logLevel = LogLevel::Warning;

private:
    static void stateChanged_cb(uv_async_t *req);
    static void write_cb(uv_write_t *req, int status);
    static void shutdown_cb(uv_shutdown_t *req, int status);

    void handleShutdown(uv_stream_t *stream, int status);
    WritePair readFromWriteQueue(const std::unique_lock<std::mutex> &);
    void processBuffer(uv_stream_t *stream, const char *data, ssize_t length);

    std::queue<WritePair> _writeQueue;
    uv_sem_t _semaphore{};
    uv_async_t _stateChanged{};
    bool _postedSemaphore = false;
    std::atomic<Handle> _lastConnectionHandle{0};
};

} // namespace Twitch::IPC
