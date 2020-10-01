// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "OperationQueue.h"

using namespace Twitch::IPC;

OperationQueue::OperationQueue()
{
    _queueThread = std::thread([this] {
        std::unique_lock lock(_mutex);
        while(!_stop) {
            _condVar.wait(lock, [this] { return !_queue.empty() || _stop; });

            if(_queue.empty() || _stop) {
                continue;
            }
            auto operation = _queue.front();
            _queue.pop();

            if(operation) {
                lock.unlock();
                operation();
                lock.lock();
            }
        }
    });
}

OperationQueue::~OperationQueue()
{
    stop();
}

void OperationQueue::stop()
{
    if(!_stop) {
        {
            std::lock_guard guard(_mutex);
            _stop = true;
            _condVar.notify_one();
        }

        if(_queueThread.joinable()) {
            _queueThread.join();
        }
    }
}

void OperationQueue::enqueue(std::function<void()> &&operation)
{
    std::lock_guard guard(_mutex);
    _queue.emplace(std::move(operation));
    _condVar.notify_one();
}
