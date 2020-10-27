// ReSharper disable CppInconsistentNaming
#pragma once

// This file contains C externs for use by C#

#if !defined(_WIN32)
#define NATIVEIPC_LIBSPEC
#elif NATIVEIPC_IMPORT
#define NATIVEIPC_LIBSPEC __declspec(dllimport)
#elif NATIVEIPC_EXPORT
#define NATIVEIPC_LIBSPEC __declspec(dllexport)
#else
#define NATIVEIPC_LIBSPEC
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void *Twitch_IPC_Connection;

NATIVEIPC_LIBSPEC Twitch_IPC_Connection Twitch_IPC_ConnectionCreateServer(const char *endpoint);
NATIVEIPC_LIBSPEC Twitch_IPC_Connection Twitch_IPC_ConnectionCreateClient(const char *endpoint);

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionDestroy(Twitch_IPC_Connection handle);

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionConnect(Twitch_IPC_Connection handle);
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionDisconnect(Twitch_IPC_Connection handle);
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionSend(
    Twitch_IPC_Connection handle, const void *bytes, int length);
NATIVEIPC_LIBSPEC uint32_t Twitch_IPC_ConnectionInvoke(
    Twitch_IPC_Connection handle, const void *bytes, int length);
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionSendResult(
    Twitch_IPC_Connection handle, uint32_t connectionId, uint32_t promiseId, const void *bytes, int length);

NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnConnect(Twitch_IPC_Connection handle, void (*fptr)());
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnDisconnect(
    Twitch_IPC_Connection handle, void (*fptr)());
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnError(Twitch_IPC_Connection handle, void (*fptr)());
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnReceived(
    Twitch_IPC_Connection handle, void (*fptr)(const void *, int));
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnInvoked(
    Twitch_IPC_Connection handle, void (*fptr)(uint32_t, uint32_t, const void *, int));
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnResult(
    Twitch_IPC_Connection handle, void (*fptr)(uint32_t, const void *, int));
NATIVEIPC_LIBSPEC void Twitch_IPC_ConnectionOnLog(Twitch_IPC_Connection handle,
    int verbosity,
    void (*fptr)(uint32_t, const char *, const char *));
#ifdef __cplusplus
}
#endif
