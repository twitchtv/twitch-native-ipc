#define NOMINMAX

#include "UVServerTransport.h"
#include "LogMacrosWithHandle.h"
#include <cassert>

using namespace Twitch::IPC;

UVServerTransport::UVServerTransport(bool latestConnectionOnly, bool allowMultiuserAccess)
    : _latestConnectionOnly(latestConnectionOnly)
    , _allowMultiuserAccess(allowMultiuserAccess)
{
}

void UVServerTransport::destroy()
{
    {
        std::lock_guard guard(_mutex);
        if(_status == Status::Listening) {
            _status = Status::Disconnecting;
            sendStateChanged(guard);
        }
    }
    if(_thread.joinable()) {
        LOG_DEBUG(0, "Waiting for disconnect to complete");
        _thread.join();
    }
    closeLoop();
}

bool UVServerTransport::listen(std::string endpoint)
{
    assert(_status == Status::Disconnected);
    assert(_endpoint.empty());
    _status = Status::Listening;
    _endpoint = std::move(endpoint);
    initSemaphore();
    _thread = std::thread([this] { runLoopThread(); });
    waitForSemaphore();
    return _status != Status::ListenFailed;
}

void UVServerTransport::send(Handle connectionHandle, Handle promiseId, Payload message)
{
    addToWriteQueue(connectionHandle, promiseId, std::move(message));
}

void UVServerTransport::broadcast(Payload message)
{
    size_t count = 0;
    decltype(_clientsByStream) clients;
    {
        std::lock_guard guard(_clientMutex);
        clients = _clientsByStream;
    }
    for(auto &i : clients) {
        if(count < clients.size() - 1) {
            send(i.second->handle, 0, message);
        } else {
            send(i.second->handle, 0, std::move(message));
            return;
        }
        ++count;
    }
}

void UVServerTransport::setLogLevel(LogLevel level)
{
    _logLevel = level;
}

int UVServerTransport::activeConnections()
{
    std::lock_guard guard(_clientMutex);
    return static_cast<int>(_clientsByStream.size());
}

void UVServerTransport::onConnect(OnHandler handler)
{
    _connectHandler = std::move(handler);
}

void UVServerTransport::onDisconnect(OnHandler handler)
{
    _disconnectHandler = std::move(handler);
}

void UVServerTransport::onData(OnDataHandler handler)
{
    _dataHandler = std::move(handler);
}

void UVServerTransport::onNoInvokeClientHandler(OnNoInvokeClientHandler handler)
{
    _noInvokeClientHandler = std::move(handler);
}

void UVServerTransport::onLog(OnLogHandler handler, LogLevel level)
{
    _logLevel = level;
    _logHandler = std::move(handler);
}

UVTransportBase::ClientInfo *UVServerTransport::getClientInfo(uv_stream_t *stream)
{
    std::lock_guard guard(_clientMutex);
    auto i = _clientsByStream.find(stream);
    return i == _clientsByStream.end() ? nullptr : i->second.get();
}

UVTransportBase::ClientInfo *UVServerTransport::getClientInfo(Handle connectionHandle)
{
    std::lock_guard guard(_clientMutex);
    for(auto &i : _clientsByStream) {
        if(i.second->handle == connectionHandle) {
            return i.second.get();
        }
    }
    return nullptr;
}

void UVServerTransport::shutdownClients()
{
    std::lock_guard guard(_clientMutex);
    for(const auto &i : _clientsByStream) {
        disconnectStream(i.second->stream, true);
    }
    _clientsByStream.clear();
}

void UVServerTransport::handleConnected(uv_stream_t *stream, int connectStatus)
{
    if (connectStatus) {
        LOG_WARNING_WITH_ERROR_CODE(0, "Accept failed early", connectStatus);
        return;
    }
    uv_stream_t *clientStream{};
    int status;
    if((status = acceptClient(stream, clientStream)) != 0) {
        LOG_WARNING_WITH_ERROR_CODE(0, "Accept failed", status);
        handleDisconnected(clientStream);
    } else {
        clientStream->data = static_cast<UVTransportBase *>(this);
        uv_read_start(clientStream, alloc_cb, read_cb);
        auto handle = getNextConnectionHandle();
        if(_latestConnectionOnly) {
            decltype(_clientsByStream) tmp;
            {
                std::lock_guard guard(_clientMutex);
                tmp.swap(_clientsByStream);
            }
            for(const auto &i : tmp) {
                disconnectStream(i.second->stream, true);
                if(_disconnectHandler) {
                    _disconnectHandler(i.second->handle);
                }
            }
        }
        {
            std::lock_guard guard(_clientMutex);
            _clientsByStream[clientStream] = std::make_shared<ClientInfo>(clientStream, handle);
        }

        LOG_DEBUG(handle, "Client connected");
        if(_connectHandler) {
            _connectHandler(handle);
        }
    }
}

void UVServerTransport::handleDisconnected(uv_stream_t *stream)
{
    const auto client = getClientInfo(stream);

    disconnectStream(stream, false);
    if(client) {
        if(_disconnectHandler) {
            _disconnectHandler(client->handle);
        }
        std::lock_guard guard(_clientMutex);
        _clientsByStream.erase(stream);
    }
}

bool UVServerTransport::isDisconnecting(const std::unique_lock<std::mutex>&)
{
    if(_status != Status::Listening) {
        if(_status == Status::Disconnecting) {
            _status = Status::Disconnected;
        }
        return true;
    }
    return false;
}

bool UVServerTransport::isConnected(const std::unique_lock<std::mutex>&)
{
    return _status == Status::Listening || _status == Status::Disconnecting;
}

void UVServerTransport::handleWrite(uv_stream_t *stream, int status)
{
    if(status < 0) {
        auto *client = getClientInfo(stream);
        if(client) {
            auto handle = client->handle;
            if (status != UV_EPIPE && status != UV_ENOTCONN) {
                LOG_ERROR_WITH_ERROR_CODE(handle, "Write failed", status);
            }
            handleDisconnected(stream);
        }
    }
}

void UVServerTransport::runLoopThread()
{
#ifndef _WIN32
    // libuv cleans up but we'll do this in case we crashed last time
    remove(_endpoint.c_str());
#endif

    LOG_INFO(0, "Listening on endpoint " + _endpoint);
    initStateChanged();

    int status;
    if((status = bind()) != 0) {
        // status codes >0 mean bad params rather than libuv error
        if (status < 0) {
            LOG_ERROR_WITH_ERROR_CODE(0, "Bind failed", status);
        }
        setStatus(Status::ListenFailed);
    } else if((status = startListening()) != 0) {
        LOG_ERROR_WITH_ERROR_CODE(0, "Listen failed", status);
        setStatus(Status::ListenFailed);
    } else {
        LOG_INFO(0, "Started successfully");
    }
    postSemaphore();
    if(_status != Status::ListenFailed) {
        uv_run(&_loop, UV_RUN_DEFAULT);
    }
    {
        std::lock_guard guard(_mutex);
        closeStateChanged(guard);
    }
    LOG_INFO(0, "Shutting down");
    closeBinder();
    shutdownClients();

    uv_run(&_loop, UV_RUN_NOWAIT);
    _endpoint.clear();
}

void UVServerTransport::setStatus(Status value)
{
    std::lock_guard guard(_mutex);
    _status = value;
}

std::string UVServerTransport::statusName(Status status)
{
    switch(status) {
    case Status::Disconnected:
        return "disconnected";
    case Status::Listening:
        return "listening";
    case Status::ListenFailed:
        return "listenFailed";
    case Status::Disconnecting:
        return "disconnecting";
    }
    return "unknown status";
}
