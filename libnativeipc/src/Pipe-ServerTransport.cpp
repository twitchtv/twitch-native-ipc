#include "Pipe-ServerTransport.h"
#include <cassert>

using namespace Twitch::IPC;

ServerTransport<Transport::Pipe>::ServerTransport(
    bool latestConnectionOnly, bool allowMultiuserAccess)
    : UVServerTransport(latestConnectionOnly, allowMultiuserAccess)
{
    _binder.data = static_cast<UVTransportBase *>(this);
}

ServerTransport<Transport::Pipe>::~ServerTransport()
{
    destroy();
}

int ServerTransport<Transport::Pipe>::acceptClient(uv_stream_t *stream, uv_stream_t *&clientStream)
{
    assert(stream == reinterpret_cast<uv_stream_t*>(&_binder));

    auto pipe = new uv_pipe_t;
    uv_pipe_init(&_loop, pipe, true);
    clientStream = reinterpret_cast<uv_stream_t*>(pipe);
    return uv_accept(stream, reinterpret_cast<uv_stream_t *>(pipe));
}

int ServerTransport<Transport::Pipe>::bind()
{
    uv_pipe_init(&_loop, &_binder, false);
    int ret = uv_pipe_bind(&_binder, _endpoint.c_str());
    if(ret == 0 && _allowMultiuserAccess) {
        ret = uv_pipe_chmod(&_binder, UV_WRITABLE | UV_READABLE);
    }

    return ret;
}

int ServerTransport<Transport::Pipe>::startListening()
{
    return uv_listen(reinterpret_cast<uv_stream_t *>(&_binder), 1, connection_cb);
}

void ServerTransport<Transport::Pipe>::closeBinder()
{
    uv_close(reinterpret_cast<uv_handle_t *>(&_binder), nullptr);
}
