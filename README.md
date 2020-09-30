# Twitch Native IPC
C++ code for our Twitch IPC

# Requirements
1. clang-format 9.X.X http://releases.llvm.org/download.html
1. CMake 3.12 or higher https://cmake.org/download/
1. Python 3.6 or higher https://www.python.org
1. Conan, install after Python using ```pip install conan```

# Usage

Twitch Native IPC is built on [libuv](https://github.com/libuv/libuv) and uses
named pipes. It is meant for communication between processes and supports a
client/server model. The server process listens on the named endpoint, and the
client will try repeatedly to connect until the server shows up. Once a connection
is established, both client and server can send messages to the other side or
invoke remote procedures and wait for the result.

Please look at [ConnectionTests.cpp](./tests/ConnectionTests.cpp) to see many examples.

## Payload

The `Twitch::IPC::Payload` type can contain strings or binary data. All of the below examples will use strings,
but the base class for `Twitch::IPC::Payload` is actually `std::vector<uint8_t>`. The constructor is very
flexible. Look at the class definition in [IConnection.h](./libnativeipc/include/nativeipc/IConnection.h) for details.

## Creating Connections

You should only need to include a single header:
```c++
#include <nativeipc/ConnectionFactory.h>
```

Connections must have a name (an endpoint). Typically it will include your application name and a purpose:
```c++
std::string endpoint = "my-apps-remote-control";
```

The server creates a connection by doing one of:
```c++
std::unique_ptr<Twitch::IPC::IConnection> connection = Twitch::IPC::newServerConnection(endpoint);
std::unique_ptr<Twitch::IPC::IServerConnection> connection = Twitch::IPC::newMulticonnectServerConnection(endpoint);
```

If you are only expecting a single client to connect, `newServerConnection` is simpler as it has the same interface
as the client connection. The handlers for the multi-connect server take one extra parameter to identify the client
to which they are conversing.

The client creates a connection by doing:
```c++
std::unique_ptr<Twitch::IPC::IConnection> connection = Twitch::IPC::newClientConnection(endpoint);
```

## Setup Handlers

Before you actually `connect` a connection, you need to hook up your callback handlers. Here are the handlers for
`IConnection`, which is the interface for both client connections and non-multiconnect server connections. You only
need to create handlers for the events you care about.

**NOTE:** The `IServerConnection` (multi-connect) versions of these all add `Twitch::IPC::Handle connectionHandle` as
the first parameter to identify the client connection.

### void onConnect(OnHandler connectHandler)

This will fire when a connection is established.
```c++
connection->onConnect([this] {
    _connected = true;
});
```

### void onDisconnect(OnHandler disconnectHandler)

This will fire when a connection is disconnected or lost from the other side.

```c++
connection->onDisconnect([this] {
    _connected = false;
});
```

### void onReceived(OnDataHandler dataHandler)

If the other side did `connection->send("hello")`, this callback will fire when it receives the message.

```c++
connection->onReceived([](Twitch::IPC::Payload data) {
    printf("Received: %s\n", data.asString().c_str());
});
```

### void onInvoked(OnInvokedPromiseIdHandler dataHandler)

There are 3 different flavors of `onInvoked` but you can only use one per connection. The first is the most manual
version. Your lambda is expected to receive the payload and a promiseId, and should return a result at some point in
the future, but outside of the lambda.

```c++
connection->onInvoked([this](Twitch::IPC::Handle connectionHandle, Twitch::IPC::Handle promiseId, Twitch::IPC::Payload message) {
    _queue.add(std::tuple(connectionHandle, promiseId, std::move(message)));
    // we'll do connection->sendResult(connectionHandle, promiseId, "life is grand") at some point in the future
});
```

### void onInvoked(OnInvokedImmediateHandler dataHandler)

The second version is the simplest but must run synchronously. Your lambda is expected to receive the payload and return
a payload immediately.

```c++
connection->onInvoked([this](Twitch::IPC::Payload message) {
    if (message.asString() == "How is life?") {
        return "Grand!";
    }
    return "Wussat now?";
});
```

### void onInvoked(OnInvokedCallbackHandler dataHandler)

The third version is the most useful. It receives a payload and a lambda to use to report your results whenever you
have figured out what that result is. You can call it immediately, or at some point in an asynchronous future.

```c++
connection->onInvoked([this](Twitch::IPC::Payload message, Twitch::IPC::ResultCallback callback) {
    if (message.asString() == "How is life?") {
        callback("Grand!");
    } else {
        callback("Wussat now?");
    }
});
```

### void onResult(OnResultHandler dataHandler)

Just as there are 3 flavors of `onInvoked`, there are two flavors of `invoke`. If you use the preferred version that
takes a lambda, you won't need `onResult`. In normal C++ code, there is little reason ever to use anything but the
version of `invoke` that includes the callback, but when creating bindings for other languages, this is often
impractical. In that case, hook up `onResult` to get the results of your simple invokes.

```c++
connection->onResult([](Twitch::IPC::Handle promiseId, Twitch::IPC::Payload message) {
    printf("Result for %d is %s\n", promisedId, message.asString().c_str());
});
``` 

### void onError(OnHandler errorHandler)

This is only called if there was a fatal error in the connection and we won't try to auto-reconnect. You should only
get this in unusual cases, such as when another process has already bound to the endpoint.

### void onLog(OnLogHandler logHandler, LogLevel level = LogLevel::None)

Use this to get log messages out of the connection.

```c++
connection->onLog(
    [this](Twitch::IPC::LogLevel level, std::string message, std::string category) {
        printf("[%s] %s: %s", Twitch::IPC::toString(level), category.c_str(), message.c_str());
    },
    Twitch::IPC::LogLevel::Info);
```

## Connect

Once you have handlers setup, go ahead and connect:

```c++
connection->connect();
```

For server connections, this will bind to the endpoint and start listening for incoming connections.
For client connections, this will attempt to connect to the endpoint. If nothing is listening yet, it will sleep for
a short interval (10ms) and try again.

Once you call `connect()` for a client, it will try very hard to stay connected. If the server goes away, the client
connection will receive `onDisconnect` and then go back into a trying-to-connect state.

## Disconnect

When you are done with a connection, you can just let it be destroyed, or you can explicitly `disconnect()`. In either
case, the other end should end up firing `onDisconnect` but this end will not.

## Sending Data (Fire And Forget)

If you want to send data but don't need a response, just use `send`:

```c++
connection->send("Ho there!");
```

Multi-connect servers can also send to all clients by doing:

```c++
connection->broadcast("Sayonara!");
```

**NOTE:** The `IServerConnection` (multi-connect) version adds `Twitch::IPC::Handle connectionHandle` as
the first parameter to identify the client connection.

```c++
connection->send(clientConnectionHandle, "Ho there!");
```

## Invoking Remote Procedures

This is the most common use case where you send off a command or query and expect a result:

```c++
connection->invoke("getSceneList", [](Twitch::IPC::InvokeResultCode resultCode, Twitch::IPC::Payload result) {
    if (resultCode != Twitch::IPC::InvokeResultCode::Good) {
        printf("RPC was cancelled due to disconnect\n");
        return;
    }
    print("Scenes are: %s\n", result.asString().c_str());
});
```

In the above example, your lambda will be called on some other thread whenever we receive the result. You should always
either check the resultCode, or at least handle empty payloads gracefully. A non-good result code will always be
accompanied by an empty payload.

There is a second form of `invoke` that is not recommended for C++ work. You can simply do:

```c++
Twitch::IPC::Handle promiseId = connection->invoke("getSceneList");
```

The result will come back in your `onResult` handler. In this case, it is up to you to do all bookkeeping.

**NOTE:** The `IServerConnection` (multi-connect) version adds `Twitch::IPC::Handle connectionHandle` as
the first parameter to identify the client connection.

# Running Tests

There are several options at the top of `ConnectionTests.cpp`:
```c
#define NUMBER_OF_REPEATS 1
#define DO_EXPLICIT_CHECKS 0
#define TEST_MANY_MESSAGES_SINGLE_DIRECTION 0
#define USE_TCP 0
#define INCLUDE_LATENCY_TEST 0
```

If you make changes, be sure to at least try setting `#define DO_EXPLICIT_CHECKS 1` and setting
`NUMBER_OF_REPEATS` to something large like 1000 or 10000. You can try 100 repeats for moderate
testing but a large number should be used to really be sure. After my single-pipe refactor, I did
about 100000 repeats.

`TEST_MANY_MESSAGES_SINGLE_DIRECTION` will add tests to send bulk messages in a single direction
instead of only bidirectionally. This is really only useful for seeing the difference in timing
delays between unidirectional and bidirectional.

If you'd like to try TCP instead of named pipes, set `USE_TCP 1`. This works very well on Unix
platforms but startup and shutdown times on Windows are pretty poor.

# Security Notes

`Twitch Native IPC` has the same security concerns as `libuv`, upon which it rests. On both Mac and Windows,
the named pipes are created as `read/write` for current user, and `read-only` for other users. This is not much
of a security concern as each connection to a named pipe is private, so `read-only` access **does not** mean
that other users can spy on conversations. Rather, it means that they could connect to a server and then
receive unsolicited communication from that server. This is not much of a threat as in most systems, the only
really sensitive information is retrieved in response to a query from the client, which can only happen if that
client has `write` access.

The most common types of messages that a server is likely to broadcast to all clients are important state changes
such as `shutdown`. If other users know that our process is shutting down or starting to stream, that is probably
of pretty low security concern.

## Protecting Against the Read-Only Issue

A very simple solution if you are still concerned is to gate server responses until at least one incoming request has
been received. Some teams have already done this in the `TypeScript` layer. Such a capability has not been added at
the native layer, but it would be a simple thing to implement.
