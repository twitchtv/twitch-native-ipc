#define NOMINMAX

#include "UVClientTransport.h"
#include "LogMacrosNoHandle.h"
#include <cassert>

using namespace Twitch::IPC;

UVClientTransport::UVClientTransport()
{
    _connect.data = this;
    _retry.data = this;
}

void UVClientTransport::destroy()
{
    if(_thread.joinable()) {
        LOG_DEBUG("Waiting for disconnect to complete");
        {
            std::lock_guard guard(_mutex);
            if(_status == Status::Connected) {
                _status = Status::Disconnecting;
                sendStateChanged(guard);
            } else if(_status == Status::Connecting) {
                _status = Status::Disconnected;
                sendStateChanged(guard);
            }
        }
        _thread.join();
    }
    closeLoop();
}

ConnectResult UVClientTransport::connect(std::string endpoint)
{
    assert(_status == Status::Disconnected);
    assert(_endpoint.empty());
    LOG_INFO("Connecting to " + endpoint);

    _status = Status::Connecting;
    _endpoint = std::move(endpoint);

    initSemaphore();
    _thread = std::thread([this] { runLoopThread(); });
    waitForSemaphore();
    
    switch(_status) {
    case Status::Connecting:
        return ConnectResult::Connecting;
    case Status::Connected:
        return ConnectResult::Connected;
    case Status::Disconnected:
    case Status::Disconnecting:
        return ConnectResult::ShuttingDown;
    case Status::WriteFailed:
        return ConnectResult::Failed;
    }
    return ConnectResult::Failed;
}

void UVClientTransport::send(Handle connectionHandle, Handle promiseId, Payload message)
{
    addToWriteQueue(connectionHandle, promiseId, std::move(message));
}

void UVClientTransport::setLogLevel(LogLevel level)
{
    _logLevel = level;
}

void UVClientTransport::onConnect(OnHandler handler)
{
    _connectHandler = std::move(handler);
}

void UVClientTransport::onDisconnect(OnHandler handler)
{
    _disconnectHandler = std::move(handler);
}

void UVClientTransport::onData(OnDataHandler handler)
{
    _dataHandler = std::move(handler);
}

void UVClientTransport::onError(OnHandler handler)
{
    _errorHandler = std::move(handler);
}

void UVClientTransport::onLog(OnLogHandler handler, LogLevel level)
{
    _logLevel = level;
    _logHandler = std::move(handler);
}

UVTransportBase::ClientInfo *UVClientTransport::getClientInfo(uv_stream_t *stream)
{
    return _clientInfo->stream == stream ? _clientInfo.get() : nullptr;
}

UVTransportBase::ClientInfo *UVClientTransport::getClientInfo(Handle connectionHandle)
{
    return _clientInfo && (!connectionHandle || _clientInfo->handle == connectionHandle) ? _clientInfo.get() : nullptr;
}

void UVClientTransport::connect_cb(uv_connect_t *req, int status)
{
    reinterpret_cast<UVClientTransport *>(req->data)->handleConnected(req->handle, status);
}

void UVClientTransport::handleConnected(uv_stream_t *stream, int status)
{
    _socket.reset(stream);
    if(!status) {
        if (_retrying) {
            uv_close(reinterpret_cast<uv_handle_t *>(&_retry), nullptr);
            _retrying = false;
        }
        if (_status == Status::Connecting) {
            assert(stream == reinterpret_cast<uv_stream_t*>(_socket.get()));
            LOG_INFO("Successfully connected to " + _endpoint);
            setStatus(Status::Connected);
            _clientInfo = std::make_unique<ClientInfo>(stream, getNextConnectionHandle());
            uv_read_start(stream, alloc_cb, read_cb);
            if(_connectHandler) {
                _connectHandler(0);
            }
            handleStateChanged();
        } else {
            closeSocket();
        }
    } else {
        closeSocket();
        if(_status == Status::Connecting) {
            if (!_retrying) {
                uv_timer_init(&_loop, &_retry);
                _retrying = true;
            }
            if(_retryDelay < 1000) {
                ++_retryDelay;
            }
            uv_timer_start(&_retry, timer_cb, _retryDelay / 10, 0);
        }
    }
}

void UVClientTransport::timer_cb(uv_timer_t *handle)
{
    auto client = reinterpret_cast<UVClientTransport *>(handle->data);
    uv_timer_stop(handle);
    client->handleTimer();
}

void UVClientTransport::handleTimer()
{
    if(_status == Status::Connecting) {
        connectSocket();
    }
}

void UVClientTransport::handleDisconnected(uv_stream_t *stream)
{
    LOG_DEBUG("Disconnected by server");

    uv_read_stop(stream);
    closeSocket();
    _clientInfo.reset();

    // If the client still expects us to be connected, start the reconnect logic
    if (_status == Status::Connected) {
        setStatus(Status::Connecting);
        connectSocket();
    }

    if(_disconnectHandler) {
        _disconnectHandler(0);
    }
}

void UVClientTransport::doDisconnectCleanup(const std::unique_lock<std::mutex>&)
{
    if (_socket) {
        disconnectStream(reinterpret_cast<uv_stream_t *>(_socket.release()), _status == Status::Disconnecting && !_retrying);
    }
    if (_retrying) {
        uv_timer_stop(&_retry);
        uv_close(reinterpret_cast<uv_handle_t *>(&_retry), nullptr);
        _retrying = false;
    }
    if(_status == Status::Disconnecting) {
        _status = Status::Disconnected;
    }
}

bool UVClientTransport::isDisconnecting(const std::unique_lock<std::mutex>&)
{
    return _status != Status::Connected && _status != Status::Connecting;
}

bool UVClientTransport::isConnected(const std::unique_lock<std::mutex>&)
{
    return _status == Status::Connected || _status == Status::Disconnecting;
}

void UVClientTransport::handleWrite(uv_stream_t *stream, int status)
{
    if(status < 0) {
        if (status == UV_EPIPE || status == UV_ENOTCONN) {
            handleDisconnected(stream);
            return;
        }

        LOG_ERROR_WITH_ERROR_CODE("Write failed", status);
        setStatus(Status::WriteFailed);
        if(_errorHandler) {
            _errorHandler(0);
        }
    }
}

void UVClientTransport::runLoopThread()
{
    _retryDelay = 20;
    if (!connectSocket()) {
        _endpoint.clear();
        _status = Status::Disconnected;
        postSemaphore();
        return;
    }
    initStateChanged();
    postSemaphore();
    uv_run(&_loop, UV_RUN_DEFAULT);
    {
        std::lock_guard guard(_mutex);
        closeStateChanged(guard);
    }
    closeSocket();
    uv_run(&_loop, UV_RUN_DEFAULT);

    _endpoint.clear();
    _clientInfo.reset();
    LOG_INFO("Connection finished with status " + statusName(_status));
}

void UVClientTransport::closeSocket()
{
    if (_socket) {
        uv_close(reinterpret_cast<uv_handle_t *>(_socket.release()), [](uv_handle_t *handle){
            delete handle;
        });
    }
}

void UVClientTransport::setStatus(Status value)
{
    std::lock_guard guard(_mutex);
    if(_status == Status::Disconnected || _status == Status::Disconnecting) {
        return;
    }
    _status = value;
}

std::string UVClientTransport::statusName(Status status)
{
    switch(status) {
    case Status::Disconnected:
        return "disconnected";
    case Status::Connecting:
        return "connecting";
    case Status::Connected:
        return "connected";
    case Status::WriteFailed:
        return "writeFailed";
    case Status::Disconnecting:
        return "disconnecting";
    }
    return "unknown status";
}
