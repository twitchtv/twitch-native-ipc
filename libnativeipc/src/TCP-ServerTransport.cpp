// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "TCP-ServerTransport.h"
#include <cassert>

using namespace Twitch::IPC;

ServerTransport<Transport::TCP>::ServerTransport(
    bool latestConnectionOnly, bool allowMultiuserAccess)
    : UVServerTransport(latestConnectionOnly, allowMultiuserAccess)
{
    _binder.data = static_cast<UVTransportBase *>(this);
}

ServerTransport<Transport::TCP>::~ServerTransport()
{
    destroy();
}

int ServerTransport<Transport::TCP>::acceptClient(uv_stream_t *stream, uv_stream_t *&clientStream)
{
    assert(stream == reinterpret_cast<uv_stream_t*>(&_binder));

    auto tcp = new uv_tcp_t;
    uv_tcp_init(&_loop, tcp);
    clientStream = reinterpret_cast<uv_stream_t*>(tcp);
    return uv_accept(stream, reinterpret_cast<uv_stream_t *>(tcp));
}

int ServerTransport<Transport::TCP>::bind()
{
    uv_tcp_init(&_loop, &_binder);

    std::string ipAddress = "0.0.0.0";
    int port = -1;
    auto i = _endpoint.find(':');
    if (i != std::string::npos) {
        port = std::atoi(_endpoint.c_str() + i + 1);
        if (i) {
            ipAddress = _endpoint.substr(0, i);
        }
    }
    if (port <= 0) {
        handleLog(0, LogLevel::Error, "Invalid address. Should be something like \"127.0.0.1:10000\" or \":10000\"");
        return 1;
    }
    
    struct sockaddr_in addr;
    if (uv_ip4_addr(ipAddress.c_str(), port, &addr)) {
        handleLog(0, LogLevel::Error, "Invalid address. Should be something like \"127.0.0.1:10000\" or \":10000\"");
        return 1;
    }
    return uv_tcp_bind(&_binder, reinterpret_cast<struct sockaddr*>(&addr), 0);
}

int ServerTransport<Transport::TCP>::startListening()
{
    return uv_listen(reinterpret_cast<uv_stream_t *>(&_binder), 1024, connection_cb);
}

void ServerTransport<Transport::TCP>::closeBinder()
{
    uv_close(reinterpret_cast<uv_handle_t *>(&_binder), nullptr);
}
