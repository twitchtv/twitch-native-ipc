// ReSharper disable CppInconsistentNaming
#include "ConnectionExports.h"
#include "ConnectionFactory.h"

// This file contains C externs for use by C#

using namespace Twitch::IPC;

NATIVEIPC_LIBSPEC Twitch_IPC_Connection Twitch_IPC_ConnectionCreateServer(const char *endpoint)
{
    return ConnectionFactory::newServerConnection(endpoint).release();
}

NATIVEIPC_LIBSPEC Twitch_IPC_Connection Twitch_IPC_ConnectionCreateClient(const char *endpoint)
{
    return ConnectionFactory::newClientConnection(endpoint).release();
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionDestroy(Twitch_IPC_Connection handle)
{
    delete reinterpret_cast<IConnection *>(handle);
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionConnect(Twitch_IPC_Connection handle)
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->connect();
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionSend(
    Twitch_IPC_Connection handle, const void *bytes, int length)
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->send({reinterpret_cast<const uint8_t *>(bytes), length});
}

NATIVEIPC_LIBSPEC uint32_t Twitch_IPC_ConnectionInvoke(
    Twitch_IPC_Connection handle, const void *bytes, int length)
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    return connection->invoke({reinterpret_cast<const uint8_t *>(bytes), length});
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionSendResult(
    Twitch_IPC_Connection handle, uint32_t connectionId, uint32_t promiseId, const void *bytes, int length)
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    return connection->sendResult(connectionId, promiseId, {reinterpret_cast<const uint8_t *>(bytes), length});
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionDisconnect(Twitch_IPC_Connection handle)
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->disconnect();
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnConnect(Twitch_IPC_Connection handle, void (*fptr)())
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->onConnect(fptr);
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnDisconnect(
    Twitch_IPC_Connection handle, void (*fptr)())
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->onDisconnect(fptr);
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnError(Twitch_IPC_Connection handle, void (*fptr)())
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    connection->onError(fptr);
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnReceived(
    Twitch_IPC_Connection handle, void (*fptr)(const void *, int))
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    if(fptr) {
        connection->onReceived(
            [fptr](Payload message) { fptr(message.data(), static_cast<int>(message.size())); });
    } else {
        connection->onReceived(nullptr);
    }
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnInvoked(
    Twitch_IPC_Connection handle, void (*fptr)(uint32_t, uint32_t, const void *, int))
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    if(fptr) {
        connection->onInvoked([fptr](Handle connectionHandle, Handle promiseId, Payload message) {
            fptr(connectionHandle, promiseId, message.data(), static_cast<int>(message.size()));
        });
    } else {
        connection->onInvoked(IConnection::OnInvokedPromiseIdHandler(nullptr));
    }
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnResult(
    Twitch_IPC_Connection handle, void (*fptr)(uint32_t, const void *, int))
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    if(fptr) {
        connection->onResult([fptr](uint32_t connectionHandle, Payload message) {
            fptr(connectionHandle, message.data(), static_cast<int>(message.size()));
        });
    } else {
        connection->onResult(nullptr);
    }
}

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnLog(
    Twitch_IPC_Connection handle, int verbosity, void (*fptr)(uint32_t, const char *, const char *))
{
    auto connection = reinterpret_cast<IConnection *>(handle);
    if(fptr) {
        connection->onLog(
            [fptr](LogLevel level, const std::string &message, const std::string &category) {
                fptr(static_cast<uint32_t>(level), message.c_str(), category.c_str());
            },
            static_cast<LogLevel>(verbosity));
    } else {
        connection->onLog(nullptr, static_cast<LogLevel>(verbosity));
    }
}
