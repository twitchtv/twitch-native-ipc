#include "Pipe-ClientTransport.h"

using namespace Twitch::IPC;

ClientTransport<Transport::Pipe>::~ClientTransport()
{
    destroy();
}

bool ClientTransport<Transport::Pipe>::connectSocket()
{
    auto pipe = new uv_pipe_t;
    pipe->data = static_cast<UVTransportBase *>(this);
    uv_pipe_init(&_loop, pipe, true);
    uv_pipe_connect(&_connect, pipe, _endpoint.c_str(), connect_cb);
    return true;
}
