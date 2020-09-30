#pragma once

#include "UVClientTransport.h"
#include "Transport.h"
#include <thread>

namespace Twitch::IPC {
template<>
class ClientTransport<Transport::TCP> final
    : public UVClientTransport {
public:
    ClientTransport() = default;
    ~ClientTransport();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ClientTransport);

protected:
    bool connectSocket() override;
};
} // namespace Twitch::IPC
