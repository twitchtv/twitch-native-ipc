#pragma once

#include "ConnectionFactoryPrivate.h"
#include "IConnection.h"
#include <atomic>
#include <mutex>

namespace Twitch::IPC {
class IClientTransport;
class IServerTransport;
class ServerConnection;
constexpr Handle ResponseFlag = 0x80000000;

class ConnectionBase {
public:
    ConnectionBase(std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint);
    virtual ~ConnectionBase() = default;
    DELETE_COPY_AND_MOVE_CONSTRUCTORS(ConnectionBase);

    static constexpr const char *DefaultCategory = "connection";

protected:
    std::shared_ptr<ConnectionFactory::Factory> _factory;
    std::shared_ptr<int> _lambdaShield;
    std::string _endpoint;
    std::mutex _transportMutex;
    std::atomic<bool> _shuttingDown{false};
    Handle getNextHandle();
    void clearLambdaShield();

private:
    std::mutex _rolloverMutex;
    std::atomic<Handle> _lastHandle{0};
};
} // namespace Twitch::IPC
