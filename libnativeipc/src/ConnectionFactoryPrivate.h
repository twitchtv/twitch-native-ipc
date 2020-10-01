// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "IClientTransport.h"
#include "IServerTransport.h"

namespace Twitch::IPC::ConnectionFactory {

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
class NATIVEIPC_LIBSPEC Factory {
public:
    Factory() = default;
    virtual ~Factory() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(Factory);

    virtual std::unique_ptr<IClientTransport> makeClientProtocol() const = 0;
    virtual std::unique_ptr<IServerTransport> makeServerProtocol(bool latestConnectionOnly, bool allowMultiuserAccess) const = 0;
};

// ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
template<typename Transport>
class NATIVEIPC_LIBSPEC TransportFactory final : public Factory {
public:
    std::unique_ptr<IClientTransport> makeClientProtocol() const override
    {
        return std::unique_ptr<IClientTransport>(std::make_unique<ClientTransport<Transport>>());
    }

    std::unique_ptr<IServerTransport> makeServerProtocol(
        bool latestConnectionOnly, bool allowMultiuserAccess) const override
    {
        return std::make_unique<ServerTransport<Transport>>(
            latestConnectionOnly, allowMultiuserAccess);
    }
};

template<typename Transport>
std::shared_ptr<Factory> MakeFactory()
{
    return std::shared_ptr<Factory>(std::make_shared<TransportFactory<Transport>>());
}

} // namespace Twitch::IPC::ConnectionFactory
