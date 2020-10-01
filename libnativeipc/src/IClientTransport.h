// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "ITransportBase.h"

namespace Twitch::IPC {
enum class ConnectResult {
    Connecting = 0,
    Connected = 1,
    ShuttingDown = 3,
    Failed = 4,
};

class IClientTransport : public ITransportBase {
public:
    IClientTransport() = default;
    virtual ~IClientTransport() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(IClientTransport);

    virtual ConnectResult connect(std::string endpoint) = 0;
    virtual void send(Handle connectionHandle, Handle promiseId, Payload message) = 0;
};

template<typename T>
class ClientTransport : public IClientTransport {
};
} // namespace Twitch::IPC
