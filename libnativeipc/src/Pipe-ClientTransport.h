// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "UVClientTransport.h"
#include "Transport.h"

namespace Twitch::IPC {
template<>
class ClientTransport<Transport::Pipe> final
    : public UVClientTransport {
public:
    ClientTransport() = default;
    ~ClientTransport();
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ClientTransport);

protected:
    bool connectSocket() override;
};
} // namespace Twitch::IPC
