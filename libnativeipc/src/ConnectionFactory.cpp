// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "ClientConnection.h"
#include "ConnectionFactoryPrivate.h"
#include "Pipe-ClientTransport.h"
#include "Pipe-ServerTransport.h"
#include "TCP-ClientTransport.h"
#include "TCP-ServerTransport.h"
#include "ServerConnection.h"
#include "ServerConnectionSingle.h"

using namespace Twitch::IPC;

namespace {

std::string pipeNameForEndpoint(const std::string &endpoint)
{
#ifdef _WIN32
    return R"(\\.\pipe\)" + endpoint;
#else
    return "/tmp/" + endpoint;
#endif
}

} // namespace

namespace Twitch::IPC::ConnectionFactory {

NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newServerConnection(const std::string &endpoint, bool allowMultiuserAccess)
{
    return std::unique_ptr<IConnection>(std::make_unique<ServerConnectionSingle>(
        MakeFactory<Transport::Pipe>(), pipeNameForEndpoint(endpoint), allowMultiuserAccess));
}

NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newClientConnection(
    const std::string &endpoint)
{
    return std::unique_ptr<IConnection>(std::make_unique<ClientConnection>(
        MakeFactory<Transport::Pipe>(), pipeNameForEndpoint(endpoint)));
}

NATIVEIPC_LIBSPEC std::unique_ptr<IServerConnection> newMulticonnectServerConnection(const std::string &endpoint, bool allowMultiuserAccess)
{
    return std::unique_ptr<IServerConnection>(std::make_unique<ServerConnection>(
        MakeFactory<Transport::Pipe>(), pipeNameForEndpoint(endpoint), false, allowMultiuserAccess));
}
} // namespace Twitch::IPC::ConnectionFactory
