// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "DeleteConstructors.h"
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace Twitch::IPC {
class OperationQueue {
public:
    OperationQueue();
    ~OperationQueue();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(OperationQueue);

    void enqueue(std::function<void()> &&operation);
    void stop();

private:
    std::queue<std::function<void()>> _queue;
    std::mutex _mutex;
    std::condition_variable _condVar;
    std::thread _queueThread;
    bool _stop = false;
};
} // namespace Twitch::IPC
