#pragma once

#include "ITransportBase.h"

namespace Twitch::IPC {
class IServerTransport : public ITransportBase {
public:
    IServerTransport() = default;
    virtual ~IServerTransport() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(IServerTransport);

    virtual bool listen(std::string endpoint) = 0;
    virtual void send(Handle connectionHandle, Handle promiseId, Payload message) = 0;
    virtual void broadcast(Payload message) = 0;
    virtual int activeConnections() = 0;
};

template<typename T>
class ServerTransport : public IServerTransport {
};
} // namespace Twitch::IPC
