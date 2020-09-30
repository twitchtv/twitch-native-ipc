#include "ClientConnection.h"
#include "ConnectionFactoryPrivate.h"
#include "Pipe-ClientTransport.h"
#include "Pipe-ServerTransport.h"
#include "TCP-ClientTransport.h"
#include "TCP-ServerTransport.h"
#include "ServerConnection.h"
#include "ServerConnectionSingle.h"

using namespace Twitch::IPC;

namespace Twitch::IPC::ConnectionFactory {
NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newServerConnectionTCP(const std::string &endpoint)
{
    return std::unique_ptr<IConnection>(std::make_unique<ServerConnectionSingle>(
            MakeFactory<Transport::TCP>(), endpoint));
}

NATIVEIPC_LIBSPEC std::unique_ptr<IConnection> newClientConnectionTCP(const std::string &endpoint)
{
    return std::unique_ptr<IConnection>(std::make_unique<ClientConnection>(
            MakeFactory<Transport::TCP>(), endpoint));
}

NATIVEIPC_LIBSPEC std::unique_ptr<IServerConnection> newMulticonnectServerConnectionTCP(const std::string &endpoint)
{
    return std::unique_ptr<IServerConnection>(std::make_unique<ServerConnection>(
            MakeFactory<Transport::TCP>(), endpoint));
}
} // namespace Twitch::IPC::ConnectionFactory
