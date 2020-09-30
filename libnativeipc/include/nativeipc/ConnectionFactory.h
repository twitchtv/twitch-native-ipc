#pragma once

#include "ConnectionExports.h"
#include "IServerConnection.h"
#include <string>

namespace Twitch::IPC::ConnectionFactory {
NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newServerConnection(const std::string &endpoint, bool allowMultiuserAccess = false);
NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newClientConnection(const std::string &endpoint);
NATIVEIPC_LIBSPEC std::unique_ptr<IServerConnection> newMulticonnectServerConnection(
    const std::string &endpoint, bool allowMultiuserAccess = false);
NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newServerConnectionTCP(const std::string &endpoint);
NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newClientConnectionTCP(const std::string &endpoint);
NATIVEIPC_LIBSPEC std::unique_ptr<IServerConnection> newMulticonnectServerConnectionTCP(const std::string &endpoint);
} // namespace Twitch::IPC::ConnectionFactory
