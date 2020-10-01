// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#define NOMINMAX

#include "UVTransportBase.h"
#include "LogMacrosWithHandle.h"
#include <cassert>

using namespace Twitch::IPC;

UVTransportBase::UVTransportBase()
{
    uv_loop_init(&_loop);
}

void UVTransportBase::closeLoop()
{
    if (uv_loop_close(&_loop)) {
        uv_run(&_loop, UV_RUN_DEFAULT); /* Run pending callbacks */
        auto result = uv_loop_close(&_loop);
        if (result) {
            uv_walk(&_loop,
                [](uv_handle_t *handle, void *) {
                if (!uv_is_closing(handle)) {
                    uv_close(handle, nullptr);
                }
            }, nullptr); /* Close all handles */
            uv_run(&_loop, UV_RUN_DEFAULT); /* Run pending callbacks */
            result = uv_loop_close(&_loop);
        }
        assert(result == 0);
    }
}

void UVTransportBase::connection_cb(uv_stream_t *stream, int status)
{
    reinterpret_cast<UVTransportBase *>(stream->data)->handleConnected(stream, status);
}

void UVTransportBase::write_cb(uv_write_t *req, int status)
{
    const auto handle = req->handle;
    const auto writeReq = reinterpret_cast<WriteRequest *>(req);
    delete writeReq;
    reinterpret_cast<UVTransportBase *>(handle->data)->handleWrite(handle, status);
}

void UVTransportBase::stateChanged_cb(uv_async_t *req)
{
    reinterpret_cast<UVTransportBase *>(req->data)->handleStateChanged();
}

void UVTransportBase::alloc_cb(uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf)
{
    reinterpret_cast<UVTransportBase *>(handle->data)->handleAlloc(handle, suggestedSize, buf);
}

void UVTransportBase::read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    reinterpret_cast<UVTransportBase *>(stream->data)->handleRead(stream, nread, buf);
}

void UVTransportBase::shutdown_cb(uv_shutdown_t *req, int status)
{
    const auto handle = req->handle;
    delete req;
    reinterpret_cast<UVTransportBase *>(handle->data)->handleShutdown(handle, status);
}

void UVTransportBase::handleAlloc(uv_handle_t *handle, size_t suggestedSize, uv_buf_t *buf)
{
    auto client = getClientInfo(handle);
    if(!client) {
        buf->base = nullptr;
        buf->len = 0;
        LOG_WARNING(0, "Allocation requested but no client attached");
        return;
    }

    if(client->receiveBuffer.size() < suggestedSize) {
        client->receiveBuffer.resize(suggestedSize);
    }
    buf->base = client->receiveBuffer.data();
    buf->len = static_cast<uint32_t>(client->receiveBuffer.size());
}

void UVTransportBase::handleRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    if(nread > 0) {
        processBuffer(stream, buf->base, nread);
    } else if(nread < 0) {
        const auto client = getClientInfo(stream);
        // if we don't get a client back, it probably means that we are a multi-receiver that actively disconnected the other side
        if(client) {
            if(nread != UV_EOF && nread != UV_ECONNRESET) {
                LOG_WARNING_WITH_ERROR_CODE(
                    client->handle, "Stream closed with error code", nread);
            }
        }
        handleDisconnected(stream);
    }
}

void UVTransportBase::handleStateChanged()
{
    std::unique_lock writeCondLock(_mutex);
    while(!_writeQueue.empty() && isConnected(writeCondLock)) {
        auto [connectionHandle, writeReq] = readFromWriteQueue(writeCondLock);
        writeCondLock.unlock();

        const auto client = getClientInfo(connectionHandle);
        if(client) {
            const auto buf = &writeReq->buf;
            uv_write(reinterpret_cast<uv_write_t *>(writeReq.release()),
                client->stream,
                buf,
                1,
                write_cb);
        } else if(_noInvokeClientHandler) {
            auto *header = reinterpret_cast<MessageHeader *>(writeReq->buf.base);
            if (header->handle) {
                _noInvokeClientHandler(connectionHandle, header->handle);
            }
        }
        writeCondLock.lock();
    }
    if(isDisconnecting(writeCondLock)) {
        doDisconnectCleanup(writeCondLock);
        uv_stop(&_loop);
    }
}

void UVTransportBase::handleLog(Handle handle, LogLevel level, std::string message)
{
    if(_logHandler && level >= _logLevel) {
        _logHandler(handle, level, message);
    }
}


void UVTransportBase::initSemaphore()
{
    _postedSemaphore = false;
    uv_sem_init(&_semaphore, 0);
}

void UVTransportBase::waitForSemaphore()
{
    uv_sem_wait(&_semaphore);
    uv_sem_destroy(&_semaphore);
    _semaphore = {};
}

void UVTransportBase::postSemaphore()
{
    if(!_postedSemaphore) {
        _postedSemaphore = true;
        uv_sem_post(&_semaphore);
    }
}

void UVTransportBase::initStateChanged()
{
    _stateChanged.data = this;
    uv_async_init(&_loop, &_stateChanged, stateChanged_cb);
}

void UVTransportBase::sendStateChanged(const std::lock_guard<std::mutex> &)
{
    if(_stateChanged.data) {
        uv_async_send(&_stateChanged);
    }
}

void UVTransportBase::closeStateChanged(const std::lock_guard<std::mutex> &)
{
    _stateChanged.data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t *>(&_stateChanged), nullptr);
}

void UVTransportBase::addToWriteQueue(Handle connectionHandle, Handle promiseId, Payload &&message)
{
    MessageHeader header{promiseId, static_cast<uint32_t>(message.size())};

    message.insert(message.begin(),
        reinterpret_cast<uint8_t *>(&header),
        reinterpret_cast<uint8_t *>(&header + 1));

    std::lock_guard guard(_mutex);
    _writeQueue.emplace(connectionHandle, new WriteRequest(std::move(message)));
    sendStateChanged(guard);
}

void UVTransportBase::disconnectStream(uv_stream_t *stream, bool shutdown)
{
    uv_read_stop(stream);
    if (shutdown) {
        uv_shutdown(new uv_shutdown_t, stream, shutdown_cb);
    } else {
        uv_close(reinterpret_cast<uv_handle_t *>(stream), [](uv_handle_t *handle) {
            delete handle;
        });
    }
}

Handle UVTransportBase::getNextConnectionHandle()
{
    auto h = ++_lastConnectionHandle;
    return h ? h : ++_lastConnectionHandle;
}

void UVTransportBase::handleShutdown(uv_stream_t *stream, int)
{
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(stream))) {
        uv_close(reinterpret_cast<uv_handle_t *>(stream), [](uv_handle_t *handle) {
            delete handle;
        });
    }
}

UVTransportBase::WritePair UVTransportBase::readFromWriteQueue(const std::unique_lock<std::mutex> &)
{
    auto writeReq = std::move(_writeQueue.front());
    _writeQueue.pop();
    return writeReq;
}

void UVTransportBase::processBuffer(uv_stream_t *stream, const char *data, ssize_t length)
{
    auto client = getClientInfo(stream);
    if(!client) {
        LOG_WARNING(0,
            "Client no longer connected. Ignoring incoming buffer of size " +
                std::to_string(length));
        return;
    }

    auto &messageBuffer = client->messageBuffer;
    auto &messageHeader = client->messageHeader;

    auto curPtr = reinterpret_cast<const uint8_t *>(data);
    const auto endPtr = curPtr + length;
    const int headerSize = sizeof(MessageHeader);

    while(curPtr < endPtr) {
        if(messageBuffer.size() < headerSize) {
            client->readToMessageBuffer(curPtr, endPtr, headerSize);
        }

        if(messageBuffer.size() < headerSize) {
            break;
        }

        if(messageHeader.bodySize == 0) {
            messageHeader = reinterpret_cast<std::vector<MessageHeader> &>(messageBuffer)[0];
            messageBuffer.clear();
        }

        client->readToMessageBuffer(curPtr, endPtr, messageHeader.bodySize);

        if(messageBuffer.size() == static_cast<size_t>(messageHeader.bodySize)) {
            std::vector<uint8_t> tmp;
            std::swap(tmp, messageBuffer);
            if(_dataHandler) {
                _dataHandler(client->handle, messageHeader.handle, std::move(tmp));
            }
            messageHeader = {};
        }
    }
}

void UVTransportBase::ClientInfo::readToMessageBuffer(
    const uint8_t *&curPtr, const uint8_t *endPtr, int finalLength)
{
    const auto bytesRemaining = static_cast<int>(endPtr - curPtr);
    const auto bytesAvailable =
        std::min(bytesRemaining, finalLength - static_cast<int>(messageBuffer.size()));

    messageBuffer.reserve(finalLength);
    messageBuffer.insert(messageBuffer.end(), curPtr, curPtr + bytesAvailable);

    curPtr += bytesAvailable;
}
