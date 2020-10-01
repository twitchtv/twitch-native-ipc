// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "UVServerTransport.h"
#include "Transport.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Twitch::IPC {
template<>
class ServerTransport<Transport::TCP> final
    : public UVServerTransport {
public:
    ServerTransport(bool latestConnectionOnly, bool allowMultiuserAccess);
    ~ServerTransport();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ServerTransport);

protected:
    int acceptClient(uv_stream_t *stream, uv_stream_t *&) override;
    int bind() override;
    int startListening() override;
    void closeBinder() override;

    uv_tcp_t _binder{};
};
} // namespace Twitch::IPC
