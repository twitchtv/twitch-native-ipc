#include "TCP-ClientTransport.h"

using namespace Twitch::IPC;

ClientTransport<Transport::TCP>::~ClientTransport()
{
    destroy();
}

bool ClientTransport<Transport::TCP>::connectSocket()
{
    std::string ipAddress = "127.0.0.1";
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
        return false;
    }

    struct sockaddr_in addr;
    if (uv_ip4_addr(ipAddress.c_str(), port, &addr)) {
        handleLog(0, LogLevel::Error, "Invalid address. Should be something like \"127.0.0.1:10000\" or \":10000\"");
        return false;
    }

    auto tcp = std::make_unique<uv_tcp_t>();
    tcp->data = static_cast<UVTransportBase *>(this);
    uv_tcp_init(&_loop, tcp.get());
    uv_tcp_connect(&_connect, tcp.release(), reinterpret_cast<struct sockaddr*>(&addr), connect_cb);
    return true;
}
