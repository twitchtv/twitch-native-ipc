// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include <chrono>
#include <thread>
#include <gtest/gtest.h>

#include "ConnectionFactory.h"
#include <atomic>
#include <random>

using namespace Twitch::IPC;
using namespace std::chrono_literals;

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

constexpr std::chrono::milliseconds OnDataSleepMilliseconds = 0ms;
constexpr std::chrono::milliseconds OnConnectSleepMilliseconds = 0ms;
constexpr std::chrono::milliseconds OnDisconnectSleepMilliseconds = 0ms;
constexpr std::chrono::milliseconds OnLogSleepMilliseconds = 0ms;
#define NUMBER_OF_REPEATS 1
#define DO_EXPLICIT_CHECKS 0
#define TEST_MANY_MESSAGES_SINGLE_DIRECTION 0
#define USE_TCP 0
#define INCLUDE_LATENCY_TEST 0

constexpr auto ManyMessageCount = 100;
constexpr auto ManyMessageSize = 10000;

class DispatchQueue {
    using Func = std::function<void()>;

public:
    DispatchQueue(size_t numberOfThreads = 0);
    ~DispatchQueue();

    void Dispatch(const Func &op);
    void Dispatch(Func &&op);
    void Wait();

    // Deleted operations
    DispatchQueue(const DispatchQueue &rhs) = delete;
    DispatchQueue &operator=(const DispatchQueue &rhs) = delete;
    DispatchQueue(DispatchQueue &&rhs) = delete;
    DispatchQueue &operator=(DispatchQueue &&rhs) = delete;

private:
    std::mutex _mutex;
    std::vector<std::thread> _threadPool;
    std::queue<Func> _jobs;
    std::condition_variable _condition;
    std::atomic_bool _shuttingDown{false};

    void ThreadHandler();
};

DispatchQueue::DispatchQueue(size_t numberOfThreads)
    : _threadPool(numberOfThreads ? numberOfThreads : std::thread::hardware_concurrency())
{
    for(auto &i : _threadPool) {
        i = std::thread(&DispatchQueue::ThreadHandler, this);
    }
}

DispatchQueue::~DispatchQueue()
{
    {
        std::lock_guard lock(_mutex);
        _shuttingDown = true;
        _condition.notify_all();
    }

    // Wait for threads to finish before we exit
    for(auto &i : _threadPool) {
        if(i.joinable()) {
            i.join();
        }
    }
}

void DispatchQueue::Dispatch(const Func &op)
{
    std::lock_guard lock(_mutex);
    _jobs.push(op);
    _condition.notify_all();
}

void DispatchQueue::Dispatch(Func &&op)
{
    std::lock_guard lock(_mutex);
    _jobs.emplace(std::move(op));
    _condition.notify_all();
}

void DispatchQueue::Wait()
{
    std::unique_lock lock(_mutex);
    _condition.wait(lock, [this] { return _jobs.empty(); });
}

void DispatchQueue::ThreadHandler()
{
    std::unique_lock lock(_mutex);
    do {
        _condition.wait(lock, [this] { return !_jobs.empty() || _shuttingDown; });

        //after wait, we own the lock
        if(!_shuttingDown && !_jobs.empty()) {
            auto func = std::move(_jobs.front());
            _jobs.pop();
            lock.unlock();

            func();

            lock.lock();
            if(_jobs.empty()) {
                // notify that there is no work remaining in case the user called Wait()
                _condition.notify_all();
            }
        }
    } while(!_shuttingDown);
}

struct TestSettings {
    std::chrono::milliseconds _sleepOnData = OnDataSleepMilliseconds;
    std::chrono::milliseconds _sleepOnConnect = OnConnectSleepMilliseconds;
    std::chrono::milliseconds _sleepOnDisconnect = OnDisconnectSleepMilliseconds;
    std::chrono::milliseconds _sleepOnLog = OnLogSleepMilliseconds;
    LogLevel _level{LogLevel::None};
    bool _writeLogs = true;
    bool _modified{};
    bool _modifiedLogging{};

    void Init()
    {
        if(!_modified) {
            _modified = true;
            _sleepOnData = 0ms;
            _sleepOnConnect = 0ms;
            _sleepOnDisconnect = 0ms;
            _sleepOnLog = 0ms;
        }
    }

    void InitLogging()
    {
        if(!_modifiedLogging) {
            _modifiedLogging = true;
            _level = LogLevel::None;
            _writeLogs = false;
        }
        Init();
    }

    TestSettings &SleepOnData(std::chrono::milliseconds ms = 1ms)
    {
        Init();
        _sleepOnData = ms;
        return *this;
    }

    TestSettings &SleepOnConnect(std::chrono::milliseconds ms = 1ms)
    {
        Init();
        _sleepOnConnect = ms;
        return *this;
    }

    TestSettings &SleepOnDisconnect(std::chrono::milliseconds ms = 1ms)
    {
        Init();
        _sleepOnDisconnect = ms;
        return *this;
    }

    TestSettings &SleepOnLog(std::chrono::milliseconds ms = 1ms)
    {
        InitLogging();
        _sleepOnLog = ms;
        return *this;
    }

    TestSettings &LogLevel(LogLevel level)
    {
        InitLogging();
        _level = level;
        return *this;
    }

    TestSettings &WriteLogs(bool b = true)
    {
        InitLogging();
        _writeLogs = b;
        return *this;
    }
};

#if USE_TCP
#define newClientConnection newClientConnectionTCP
#define newServerConnection newServerConnectionTCP
#define newMulticonnectServerConnection newMulticonnectServerConnectionTCP
#endif

class NativeIPCTestBase : public ::testing::TestWithParam<TestSettings> {
public:
    using LogVector = std::vector<std::tuple<Handle, LogLevel, std::string, std::string>>;

    void SetUp() override
    {
        auto params = GetParam();
        _sleepOnData = params._sleepOnData;
        _sleepOnConnect = params._sleepOnConnect;
        _sleepOnDisconnect = params._sleepOnDisconnect;
        _sleepOnLog = params._sleepOnLog;
        _level = params._level;
        _writeLogs = params._writeLogs;
        clientConnection = MakeClient();
        std::lock_guard guard(s_logMutex);
        s_clientLogMessages.clear();
    }

    virtual void HookClient(IConnection *connection)
    {
        connection->onError([] {FAIL();});
        if (_sleepOnData.count()) {
            connection->onReceived([this](Payload) { Sleep(_sleepOnData); });
            connection->onInvoked([this](Handle, Handle, Payload) { Sleep(_sleepOnData); });
            connection->onResult([this](Handle, Payload) { Sleep(_sleepOnData); });
        }
        if (_sleepOnConnect.count()) {
            connection->onConnect([this] { Sleep(_sleepOnConnect); });
        }
        if (_sleepOnDisconnect.count()) {
            connection->onDisconnect([this] { Sleep(_sleepOnDisconnect); });
        }
        connection->onLog(
            [this](LogLevel level, std::string message, std::string category) {
                Log("Client", level, message, category);
                std::lock_guard guard(s_logMutex);
                s_clientLogMessages.emplace_back(0, level, std::move(message), std::move(category));
            },
            _level);
    }

    static void Sleep(std::chrono::milliseconds msToSleep)
    {
        if(msToSleep != 0ms) {
            std::this_thread::sleep_for(msToSleep);
        }
    }

    void Log(const char *source,
        LogLevel level,
        const std::string &message,
        const std::string &category,
        Handle handle = 0) const
    {
        Sleep(_sleepOnLog);
        if(!_writeLogs)
            return;
        if(handle) {
            printf("%d: ", static_cast<int>(handle));
        }
        printf("%s - [%s] %s (%s)\n", source, toString(level), message.c_str(), category.c_str());
    }

    static int WaitForExpectation(bool gte, int expectedCount, std::atomic_int &counter, double timeoutSeconds = 1.0)
    {
        auto start = std::chrono::steady_clock::now();
        while(gte ? counter < expectedCount : counter != expectedCount) {
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            if(elapsed.count() >= timeoutSeconds) {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
        return counter;
    }

    static int WaitForExpectation(bool gte, int expectedCount, LogVector &logVector, double timeoutSeconds = 1.0)
    {
        auto start = std::chrono::steady_clock::now();
        std::unique_lock guard(s_logMutex);
        while(gte ? static_cast<int>(logVector.size()) < expectedCount : static_cast<int>(logVector.size()) != expectedCount) {
            guard.unlock();
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            if(elapsed.count() >= timeoutSeconds) {
                break;
            }
            std::this_thread::sleep_for(1ms);
            guard.lock();
        }
        return static_cast<int>(logVector.size());
    }

    static int WaitForExpectation(bool gte, int expectedCount, std::function<int()> callback, double timeoutSeconds = 1.0)
    {
        auto start = std::chrono::steady_clock::now();
        std::unique_lock guard(s_logMutex);
        while(gte ? callback() < expectedCount : callback() != expectedCount) {
            guard.unlock();
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            if(elapsed.count() >= timeoutSeconds) {
                break;
            }
            std::this_thread::sleep_for(1ms);
            guard.lock();
        }
        return callback();
    }

    std::unique_ptr<IConnection> MakeClient()
    {
        auto client = ConnectionFactory::newClientConnection(s_EndPoint);
        HookClient(client.get());
        return client;
    }

    std::shared_ptr<IConnection> clientConnection;
    static LogVector s_clientLogMessages;
    static std::mutex s_logMutex;

    static const char *s_EndPoint;
    LogLevel _level{LogLevel::None};
    std::chrono::milliseconds _sleepOnData{};
    std::chrono::milliseconds _sleepOnConnect{};
    std::chrono::milliseconds _sleepOnDisconnect{};
    std::chrono::milliseconds _sleepOnLog{};
    bool _writeLogs{};
};

#if USE_TCP
const char *NativeIPCTestBase::s_EndPoint = "127.0.0.1:10000";
#else
const char *NativeIPCTestBase::s_EndPoint = "twitch-native-ipc.test.endpoint.sock";
#endif

NativeIPCTestBase::LogVector NativeIPCTestBase::s_clientLogMessages;
std::mutex NativeIPCTestBase::s_logMutex;

class NativeIPCTest : public NativeIPCTestBase {
public:
    void SetUp() override
    {
        NativeIPCTestBase::SetUp();
        serverConnection = MakeServer();
    }

    std::unique_ptr<IConnection> MakeServer()
    {
        auto server = ConnectionFactory::newServerConnection(s_EndPoint);
        HookServer(server.get());
        return server;
    }

    virtual void HookServer(IConnection *connection)
    {
        connection->onError([] {FAIL();});
        if (_sleepOnData.count()) {
            connection->onReceived([this](Payload) { Sleep(_sleepOnData); });
            connection->onInvoked([this](Handle, Handle, Payload) { Sleep(_sleepOnData); });
            connection->onResult([this](Handle, Payload) { Sleep(_sleepOnData); });
        }
        if (_sleepOnConnect.count()) {
            connection->onConnect([this] { Sleep(_sleepOnConnect); });
        }
        if (_sleepOnDisconnect.count()) {
            connection->onDisconnect([this] { Sleep(_sleepOnDisconnect); });
        }
        connection->onLog(
            [this](LogLevel level, std::string message, std::string category) {
                Log("Server", level, message, category);
                std::lock_guard guard(s_logMutex);
                s_serverLogMessages.emplace_back(0, level, std::move(message), std::move(category));
            },
            _level);
        std::lock_guard guard(s_logMutex);
        s_serverLogMessages.clear();
    }

    std::shared_ptr<IConnection> serverConnection;
    static LogVector s_serverLogMessages;
};

NativeIPCTestBase::LogVector NativeIPCTest::s_serverLogMessages;

using TransmitTest = NativeIPCTest;

class MultiConnectIPCTest : public NativeIPCTestBase {
public:
    void SetUp() override
    {
        NativeIPCTestBase::SetUp();
        serverConnection = MakeServer();
    }

    std::unique_ptr<IServerConnection> MakeServer()
    {
        auto server = ConnectionFactory::newMulticonnectServerConnection(s_EndPoint);
        HookServer(server.get());
        return server;
    }

    virtual void HookServer(IServerConnection *connection)
    {
        connection->onError([](Handle) {FAIL();});
        connection->onLog(
            [this](Handle handle, LogLevel level, std::string message, std::string category) {
                Log("Server", level, message, category, handle);
                std::lock_guard guard(s_logMutex);
                s_serverLogMessages.emplace_back(handle, level, std::move(message), std::move(category));
            },
            _level);
        std::lock_guard guard(s_logMutex);
        s_serverLogMessages.clear();
    }

    std::shared_ptr<IServerConnection> serverConnection;
    static LogVector s_serverLogMessages;
};

using MultiTransmitTest = MultiConnectIPCTest;

NativeIPCTestBase::LogVector MultiConnectIPCTest::s_serverLogMessages;

class ConnectDisconnectTest : public NativeIPCTest {
public:
    std::atomic_int clientConnects{0};
    std::atomic_int serverConnects{0};
    std::atomic_int clientDisconnects{0};
    std::atomic_int serverDisconnects{0};

    void HookClient(IConnection *connection) override
    {
        NativeIPCTest::HookClient(connection);
        connection->onConnect([this] {
            Sleep(_sleepOnConnect);
            ++this->clientConnects;
        });
        connection->onDisconnect([this] {
            Sleep(_sleepOnDisconnect);
            ++this->clientDisconnects;
        });
    }

    void HookServer(IConnection *connection) override
    {
        NativeIPCTest::HookServer(connection);
        connection->onConnect([this] {
            Sleep(_sleepOnConnect);
            ++this->serverConnects;
        });
        connection->onDisconnect([this] {
            Sleep(_sleepOnDisconnect);
            ++this->serverDisconnects;
        });
    }
};

class MultiConnectDisconnectTest : public MultiConnectIPCTest {
public:
    std::atomic_int clientConnects{0};
    std::atomic_int serverConnects{0};
    std::atomic_int clientDisconnects{0};
    std::atomic_int serverDisconnects{0};

    void HookClient(IConnection *connection) override
    {
        MultiConnectIPCTest::HookClient(connection);
        connection->onConnect([this] {
            Sleep(_sleepOnConnect);
            ++this->clientConnects;
        });
        connection->onDisconnect([this] {
            Sleep(_sleepOnDisconnect);
            ++this->clientDisconnects;
        });
    }

    void HookServer(IServerConnection *connection) override
    {
        MultiConnectIPCTest::HookServer(connection);
        connection->onConnect([this](Handle) {
            Sleep(_sleepOnConnect);
            ++this->serverConnects;
        });
        connection->onDisconnect([this](Handle) {
            Sleep(_sleepOnDisconnect);
            ++this->serverDisconnects;
        });
    }
};

#define WAIT_UNTIL_EQ(expected, ...)                                                               \
    do {                                                                                           \
        int result = WaitForExpectation(false, expected, __VA_ARGS__);                             \
        EXPECT_EQ(expected, result);                                                               \
    } while(false)

#define WAIT_UNTIL_REACHES(expected, ...)                                                          \
    assert(expected != 0);                                                                         \
    do {                                                                                           \
        int result = WaitForExpectation(true, expected, __VA_ARGS__);                              \
        EXPECT_EQ(expected, result);                                                               \
    } while(false)

/********* TESTS *********/

TEST_P(ConnectDisconnectTest, TearDownServerEarlyTest)
{
    // no connection should happen
    serverConnection->connect();
    serverConnection.reset();
}

TEST_P(ConnectDisconnectTest, TearDownClientEarlyTest)
{
    clientConnection->connect();
    clientConnection.reset();
}

TEST_P(ConnectDisconnectTest, TearDownClientAndServerEarlyTest)
{
    // no connection should happen
    serverConnection->connect();
    serverConnection.reset();

    clientConnection->connect();
    clientConnection.reset();
    if(GetParam()._modified) {
        std::this_thread::sleep_for(100ms);
    } else {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(0, clientConnects);
    EXPECT_EQ(0, clientDisconnects);
    EXPECT_EQ(0, serverConnects);
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(MultiConnectDisconnectTest, TearDownEarlyTest)
{
    // no connection should happen
    serverConnection->connect();
    serverConnection.reset();

    clientConnection->connect();
    clientConnection.reset();
    if(GetParam()._modified) {
        std::this_thread::sleep_for(100ms);
    } else {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(0, clientConnects);
    EXPECT_EQ(0, clientDisconnects);
    EXPECT_EQ(0, serverConnects);
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(ConnectDisconnectTest, ResetServerTest)
{
    // since server reset, client should have received
    // a disconnect message but server should not have
    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects);
    WAIT_UNTIL_REACHES(1, serverConnects);

    serverConnection.reset();
    WAIT_UNTIL_REACHES(1, clientDisconnects);

    clientConnection.reset();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(MultiConnectDisconnectTest, ResetServerTest)
{
    // since server reset, client should have received
    // a disconnect message but server should not have
    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects);
    WAIT_UNTIL_REACHES(1, serverConnects);

    serverConnection.reset();
    WAIT_UNTIL_REACHES(1, clientDisconnects);

    clientConnection.reset();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(ConnectDisconnectTest, DisconnectServerTest)
{
    // since server initiated disconnect, client should have received
    // a disconnect message but server should not have
    // NOTE: Disconnect and reset should be identical in their
    //       communication pathways

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects);
    WAIT_UNTIL_REACHES(1, serverConnects);

    serverConnection->disconnect();
    WAIT_UNTIL_REACHES(1, clientDisconnects, 10);

    clientConnection->disconnect();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(MultiConnectDisconnectTest, DisconnectServerTest)
{
    // since server initiated disconnect, client should have received
    // a disconnect message but server should not have
    // NOTE: Disconnect and reset should be identical in their
    //       communication pathways

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects);
    WAIT_UNTIL_REACHES(1, serverConnects);

    serverConnection->disconnect();
    WAIT_UNTIL_REACHES(1, clientDisconnects, 10);

    clientConnection->disconnect();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(ConnectDisconnectTest, ResetClientTest)
{
    // since client reset, server should have received
    // a disconnect message but client should not have

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    clientConnection.reset();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection.reset();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(MultiConnectDisconnectTest, ResetClientTest)
{
    // since client reset, server should have received
    // a disconnect message but client should not have

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    clientConnection.reset();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection.reset();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(ConnectDisconnectTest, DisconnectClientTest)
{
    // since client initiated disconnect, server should have received
    // a disconnect message but client should not have
    // NOTE: Disconnect and reset should be identical in their
    //       communication pathways

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    clientConnection->disconnect();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection->disconnect();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(MultiConnectDisconnectTest, DisconnectClientTest)
{
    // since client initiated disconnect, server should have received
    // a disconnect message but client should not have
    // NOTE: Disconnect and reset should be identical in their
    //       communication pathways

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    clientConnection->disconnect();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection->disconnect();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(ConnectDisconnectTest, DisconnectAndResetServerTest)
{
    clientConnection->connect();
    std::this_thread::sleep_for(10ms);
    serverConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    serverConnection->disconnect();
    WAIT_UNTIL_REACHES(1, clientDisconnects, 10);

    clientConnection->disconnect();
    EXPECT_EQ(0, serverDisconnects);

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(2, clientConnects, 10);
    WAIT_UNTIL_REACHES(2, serverConnects, 10);

    serverConnection.reset();
    WAIT_UNTIL_REACHES(2, clientDisconnects, 10);
    WAIT_UNTIL_REACHES(2, clientConnects, 10);

    clientConnection.reset();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(MultiConnectDisconnectTest, DisconnectAndResetServerTest)
{
    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    serverConnection->disconnect();
    WAIT_UNTIL_REACHES(1, clientDisconnects, 10);

    clientConnection->disconnect();
    EXPECT_EQ(0, serverDisconnects);

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(2, clientConnects, 10);
    WAIT_UNTIL_REACHES(2, serverConnects, 10);

    serverConnection.reset();
    WAIT_UNTIL_REACHES(2, clientDisconnects, 10);

    clientConnection.reset();
    EXPECT_EQ(0, serverDisconnects);
}

TEST_P(ConnectDisconnectTest, DisconnectAndResetClientTest)
{
    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    clientConnection->disconnect();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection->disconnect();
    EXPECT_EQ(0, clientDisconnects);

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(2, clientConnects, 10);
    WAIT_UNTIL_REACHES(2, serverConnects, 10);

    clientConnection.reset();
    WAIT_UNTIL_REACHES(2, serverDisconnects, 10);

    serverConnection.reset();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(MultiConnectDisconnectTest, DisconnectAndResetClientTest)
{
    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    WAIT_UNTIL_REACHES(1, serverConnects, 10);

    clientConnection->disconnect();
    WAIT_UNTIL_REACHES(1, serverDisconnects, 10);

    serverConnection->disconnect();
    EXPECT_EQ(0, clientDisconnects);

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(2, clientConnects, 10);
    WAIT_UNTIL_REACHES(2, serverConnects, 10);

    clientConnection.reset();
    WAIT_UNTIL_REACHES(2, serverDisconnects, 10);

    serverConnection.reset();
    EXPECT_EQ(0, clientDisconnects);
}

TEST_P(ConnectDisconnectTest, RepeatedConnectionTest)
{
    // only the first connect should result in onConnect being called
    serverConnection->connect();

    for(auto i = 0; i < 5; ++i) {
        clientConnection->connect();
    }
    Sleep(10ms);
    WAIT_UNTIL_REACHES(1, clientConnects, 20);
}

TEST_P(MultiConnectDisconnectTest, RepeatedConnectionTest)
{
    // only the first connect should result in onConnect being called
    serverConnection->connect();

    for(auto i = 0; i < 5; ++i) {
        clientConnection->connect();
    }
    WAIT_UNTIL_REACHES(1, clientConnects, 20);
}

TEST_P(ConnectDisconnectTest, ClientDisconnectTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    clientConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        if(++this->clientConnects < expectedConnectCount) {
            this->clientConnection->disconnect();
            this->clientConnection->connect();
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, serverDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(MultiConnectDisconnectTest, ClientDisconnectTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    clientConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        if(++this->clientConnects < expectedConnectCount) {
            this->clientConnection->disconnect();
            this->clientConnection->connect();
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, serverDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(ConnectDisconnectTest, ServerDisconnectTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        if(++this->serverConnects < expectedConnectCount) {
            this->serverConnection->disconnect();
            Sleep(2ms);
            this->serverConnection->connect();
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, clientDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(MultiConnectDisconnectTest, ServerDisconnectTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->onConnect([&](Handle) {
        Sleep(_sleepOnConnect);
        if(++this->serverConnects < expectedConnectCount) {
            this->serverConnection->disconnect();
            Sleep(2ms);
            this->serverConnection->connect();
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, clientDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(ConnectDisconnectTest, ClientMultipleResetTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->connect();
    clientConnection->connect();

    for (auto i=1; i<expectedConnectCount; ++i) {
        WAIT_UNTIL_REACHES(i, clientConnects, 20);
        WAIT_UNTIL_REACHES(i, serverConnects);
        clientConnection.reset();
        clientConnection = MakeClient();
        clientConnection->connect();
    }

    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, serverDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(MultiConnectDisconnectTest, ClientMultipleResetTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->connect();
    clientConnection->connect();

    for (auto i=1; i<expectedConnectCount; ++i) {
        WAIT_UNTIL_REACHES(i, clientConnects, 20);
        WAIT_UNTIL_REACHES(i, serverConnects);
        clientConnection.reset();
        clientConnection = MakeClient();
        clientConnection->connect();
    }

    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, serverDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(ConnectDisconnectTest, ServerMultipleResetTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->connect();
    clientConnection->connect();

    for (auto i=1; i<expectedConnectCount; ++i) {
        WAIT_UNTIL_REACHES(i, serverConnects, 20);
        WAIT_UNTIL_REACHES(i, clientConnects);
        serverConnection.reset();
        serverConnection = MakeServer();
        serverConnection->connect();
    }

    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, clientDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(MultiConnectDisconnectTest, ServerMultipleResetTest)
{
    // test repeatedly connecting and disconnecting
    const auto expectedConnectCount = 3;
    const auto expectedDisconnectCount = expectedConnectCount - 1;

    serverConnection->connect();
    clientConnection->connect();

    for (auto i=1; i<expectedConnectCount; ++i) {
        WAIT_UNTIL_REACHES(i, serverConnects, 20);
        WAIT_UNTIL_REACHES(i, clientConnects);
        serverConnection.reset();
        serverConnection = MakeServer();
        serverConnection->connect();
    }

    WAIT_UNTIL_REACHES(expectedConnectCount, serverConnects, 20);
    WAIT_UNTIL_REACHES(expectedConnectCount, clientConnects);
    WAIT_UNTIL_REACHES(expectedDisconnectCount, clientDisconnects);
    if(_level < LogLevel::Warning) {
        EXPECT_GE(static_cast<int>(s_serverLogMessages.size()), 1);
        EXPECT_GE(static_cast<int>(s_clientLogMessages.size()), 1);
    }
}

TEST_P(ConnectDisconnectTest, ServerConnectBlockedAfterShutdownTest)
{
    std::atomic_bool isResetting = false;
    auto serverConnectionPtr = serverConnection.get();
    serverConnection->onConnect([&] {
        ++this->serverConnects;
        while (!isResetting) {
            Sleep(1ms);
        }
        Sleep(10ms);
        serverConnectionPtr->disconnect();
        serverConnectionPtr->connect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, serverConnects, 10);
    isResetting = true;
    serverConnection.reset();
    Sleep(10ms);
    WAIT_UNTIL_REACHES(1, clientConnects);
}

TEST_P(ConnectDisconnectTest, ClientConnectBlockedAfterShutdownTest)
{
    std::atomic_bool isResetting = false;
    auto clientConnectionPtr = clientConnection.get();
    clientConnection->onConnect([&] {
        ++this->clientConnects;
        while (!isResetting) {
            Sleep(1ms);
        }
        Sleep(10ms);
        clientConnectionPtr->disconnect();
        clientConnectionPtr->connect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnects, 10);
    isResetting = true;
    clientConnection.reset();
    Sleep(10ms);
    WAIT_UNTIL_REACHES(1, serverConnects);
}

TEST_P(TransmitTest, ShortAndLongMessageTest)
{
    std::vector<uint8_t> shortMessage(1000);
    std::vector<uint8_t> longMessage(50000);

    shortMessage[shortMessage.size() - 1] = '\0';
    longMessage[longMessage.size() - 1] = '\0';

    for(size_t i = 0; i < shortMessage.size() - 1; ++i) {
        shortMessage[i] = static_cast<char>(rand() % 28) + 97;
    }

    for(size_t i = 0; i < longMessage.size() - 1; ++i) {
        longMessage[i] = static_cast<char>(rand() % 28) + 97;
    }
    serverConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        this->serverConnection->send(shortMessage);
        this->serverConnection->send(longMessage);
    });

    std::atomic_int gotShort{0};
    std::atomic_int gotLong{0};
    std::atomic_int gotOther{0};

    clientConnection->onReceived([&](Payload data) {
        Sleep(_sleepOnData);
        if(data.size() == longMessage.size()) {
            EXPECT_EQ(data, longMessage);
            ++gotLong;
        } else if(data.size() == shortMessage.size()) {
            EXPECT_EQ(data, shortMessage);
            ++gotShort;
        } else {
            ++gotOther;
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, gotShort, 20);
    WAIT_UNTIL_REACHES(1, gotLong, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(MultiTransmitTest, ShortAndLongMessageTest)
{
    std::vector<uint8_t> shortMessage(1000);
    std::vector<uint8_t> longMessage(50000);

    shortMessage[shortMessage.size() - 1] = '\0';
    longMessage[longMessage.size() - 1] = '\0';

    for(size_t i = 0; i < shortMessage.size() - 1; ++i) {
        shortMessage[i] = static_cast<char>(rand() % 28) + 97;
    }

    for(size_t i = 0; i < longMessage.size() - 1; ++i) {
        longMessage[i] = static_cast<char>(rand() % 28) + 97;
    }
    serverConnection->onConnect([&](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        this->serverConnection->send(connectionHandle, shortMessage);
        this->serverConnection->send(connectionHandle, longMessage);
    });

    std::atomic_int gotShort{0};
    std::atomic_int gotLong{0};
    std::atomic_int gotOther{0};

    clientConnection->onReceived([&](Payload data) {
        Sleep(_sleepOnData);
        if(data.size() == longMessage.size()) {
            EXPECT_EQ(data, longMessage);
            ++gotLong;
        } else if(data.size() == shortMessage.size()) {
            EXPECT_EQ(data, shortMessage);
            ++gotShort;
        } else {
            ++gotOther;
        }
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, gotShort, 20);
    WAIT_UNTIL_REACHES(1, gotLong, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(TransmitTest, SendInClientOnConnectTest)
{
    std::atomic_int gotTest{0};
    std::atomic_int gotLonger{0};
    std::atomic_int gotOther{0};

    serverConnection->onReceived([&](Payload data) {
        Sleep(_sleepOnData);
        if(data.asString() == "test") {
            ++gotTest;
        } else if(data.asString() == "longer") {
            ++gotLonger;
        } else {
            ++gotOther;
        }
    });

    clientConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        clientConnection->send("test");
        clientConnection->send("longer");
    });

    clientConnection->connect();
    serverConnection->connect();

    WAIT_UNTIL_REACHES(1, gotTest, 20);
    WAIT_UNTIL_REACHES(1, gotLonger, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(MultiTransmitTest, SendInClientOnConnectTest)
{
    std::atomic_int gotTest{0};
    std::atomic_int gotLonger{0};
    std::atomic_int gotOther{0};

    serverConnection->onReceived([&](Handle, Payload data) {
        Sleep(_sleepOnData);
        if(data.asString() == "test") {
            ++gotTest;
        } else if(data.asString() == "longer") {
            ++gotLonger;
        } else {
            ++gotOther;
        }
    });

    clientConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        clientConnection->send("test");
        clientConnection->send("longer");
    });

    clientConnection->connect();
    serverConnection->connect();

    WAIT_UNTIL_REACHES(1, gotTest, 20);
    WAIT_UNTIL_REACHES(1, gotLonger, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(TransmitTest, SendInServerOnConnectTest)
{
    std::atomic_int gotTest{0};
    std::atomic_int gotLonger{0};
    std::atomic_int gotOther{0};

    clientConnection->onReceived([&](Payload data) {
        Sleep(_sleepOnData);
        if(data.asString() == "test") {
            ++gotTest;
        } else if(data.asString() == "longer") {
            ++gotLonger;
        } else {
            ++gotOther;
        }
    });

    serverConnection->onConnect([&] {
        Sleep(_sleepOnConnect);
        serverConnection->send("test");
        serverConnection->send("longer");
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, gotTest, 20);
    WAIT_UNTIL_REACHES(1, gotLonger, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(MultiTransmitTest, SendInServerOnConnectTest)
{
    std::atomic_int gotTest{0};
    std::atomic_int gotLonger{0};
    std::atomic_int gotOther{0};

    clientConnection->onReceived([&](Payload data) {
        Sleep(_sleepOnData);
        if(data.asString() == "test") {
            ++gotTest;
        } else if(data.asString() == "longer") {
            ++gotLonger;
        } else {
            ++gotOther;
        }
    });

    serverConnection->onConnect([&](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        serverConnection->send(connectionHandle, "test");
        serverConnection->send(connectionHandle, "longer");
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, gotTest, 20);
    WAIT_UNTIL_REACHES(1, gotLonger, 20);
    EXPECT_EQ(0, gotOther);
}

TEST_P(TransmitTest, ManyBidirectionalMessagesTest)
{
    std::atomic_int gotClientData{0};
    std::atomic_int gotServerData{0};
    std::atomic_int sentData{0};
    std::atomic_int clientConnected{0};
    std::atomic_int serverConnected{0};

    // we don't have all day...
    if(_sleepOnData == OnDataSleepMilliseconds && !GetParam()._modified) {
        _sleepOnData = 0ms;
    } else if(_sleepOnData > 2ms) {
        _sleepOnData = 2ms;
    }
    serverConnection->onReceived([&](Payload) {
        Sleep(_sleepOnData);
        ++gotClientData;
    });

    clientConnection->onReceived([&](Payload) {
        Sleep(_sleepOnData);
        ++gotServerData;
    });

    clientConnection->onConnect([this, &clientConnected] {
        Sleep(_sleepOnConnect);
        ++clientConnected;
    });
    serverConnection->onConnect([this, &serverConnected] {
        Sleep(_sleepOnConnect);
        ++serverConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnected, 10);
    WAIT_UNTIL_REACHES(1, serverConnected, 10);

    {
        DispatchQueue queue;
        for(auto i = 0; i < ManyMessageCount; ++i) {
            queue.Dispatch([&] {
                this->serverConnection->send(std::vector<uint8_t>(ManyMessageSize));
                this->clientConnection->send(std::vector<uint8_t>(ManyMessageSize));
                ++sentData;
            });
        }
        queue.Wait();
    }

    WAIT_UNTIL_REACHES(ManyMessageCount, sentData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotClientData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotServerData, 60);
}

TEST_P(MultiTransmitTest, ManyBidirectionalMessagesTest)
{
    std::atomic_int gotClientData{0};
    std::atomic_int gotServerData{0};
    std::atomic_int sentData{0};
    std::atomic_int clientConnected{0};
    std::atomic_int serverConnected{0};

    // we don't have all day...
    if(_sleepOnData == OnDataSleepMilliseconds && !GetParam()._modified) {
        _sleepOnData = 0ms;
    } else if(_sleepOnData > 2ms) {
        _sleepOnData = 2ms;
    }
    serverConnection->onReceived([&](Handle, Payload) {
        Sleep(_sleepOnData);
        ++gotClientData;
    });

    clientConnection->onReceived([&](Payload) {
        Sleep(_sleepOnData);
        ++gotServerData;
    });

    clientConnection->onConnect([this, &clientConnected] {
        Sleep(_sleepOnConnect);
        ++clientConnected;
    });
    serverConnection->onConnect([this, &serverConnected](Handle){
        Sleep(_sleepOnConnect);
        ++serverConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnected, 10);
    WAIT_UNTIL_REACHES(1, serverConnected, 10);

    {
        DispatchQueue queue;
        for(auto i = 0; i < ManyMessageCount; ++i) {
            queue.Dispatch([&] {
                this->serverConnection->broadcast(std::vector<uint8_t>(ManyMessageSize));
                this->clientConnection->send(std::vector<uint8_t>(ManyMessageSize));
                ++sentData;
            });
        }
        queue.Wait();
    }

    WAIT_UNTIL_REACHES(ManyMessageCount, sentData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotClientData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotServerData, 60);
}

#if TEST_MANY_MESSAGES_SINGLE_DIRECTION
TEST_P(MultiTransmitTest, ManyServerMessagesTest)
{
    std::atomic_int gotServerData{0};
    std::atomic_int sentData{0};
    std::atomic_int clientConnected{0};
    std::atomic_int serverConnected{0};

    // we don't have all day...
    if(_sleepOnData == OnDataSleepMilliseconds && !GetParam()._modified) {
        _sleepOnData = 0ms;
    } else if(_sleepOnData > 2ms) {
        _sleepOnData = 2ms;
    }
    clientConnection->onReceived([&](Payload) {
        Sleep(_sleepOnData);
        ++gotServerData;
    });

    clientConnection->onConnect([this, &clientConnected] {
        Sleep(_sleepOnConnect);
        ++clientConnected;
    });
    serverConnection->onConnect([this, &serverConnected](Handle){
        Sleep(_sleepOnConnect);
        ++serverConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnected, 10);
    WAIT_UNTIL_REACHES(1, serverConnected, 10);

    {
        DispatchQueue queue;
        for(auto i = 0; i < ManyMessageCount; ++i) {
            queue.Dispatch([&] {
                this->serverConnection->broadcast(std::vector<uint8_t>(ManyMessageSize));
                ++sentData;
            });
        }
        queue.Wait();
    }

    WAIT_UNTIL_REACHES(ManyMessageCount, sentData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotServerData, 60);
}

TEST_P(MultiTransmitTest, ManyClientMessagesTest)
{
    std::atomic_int gotClientData{0};
    std::atomic_int sentData{0};
    std::atomic_int clientConnected{0};
    std::atomic_int serverConnected{0};

    // we don't have all day...
    if(_sleepOnData == OnDataSleepMilliseconds && !GetParam()._modified) {
        _sleepOnData = 0ms;
    } else if(_sleepOnData > 2ms) {
        _sleepOnData = 2ms;
    }
    serverConnection->onReceived([&](Handle, Payload) {
        Sleep(_sleepOnData);
        ++gotClientData;
    });

    clientConnection->onConnect([this, &clientConnected] {
        Sleep(_sleepOnConnect);
        ++clientConnected;
    });
    serverConnection->onConnect([this, &serverConnected](Handle){
        Sleep(_sleepOnConnect);
        ++serverConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientConnected, 10);
    WAIT_UNTIL_REACHES(1, serverConnected, 10);

    {
        DispatchQueue queue;
        for(auto i = 0; i < ManyMessageCount; ++i) {
            queue.Dispatch([&] {
                this->clientConnection->send(std::vector<uint8_t>(ManyMessageSize));
                ++sentData;
            });
        }
        queue.Wait();
    }

    WAIT_UNTIL_REACHES(ManyMessageCount, sentData, 60);
    WAIT_UNTIL_REACHES(ManyMessageCount, gotClientData, 60);
}
#endif

#if INCLUDE_LATENCY_TEST
TEST_P(TransmitTest, InvokeLatencyTest)
{
    int receivesExpected = GetParam()._modified ? 10 : 500;
    std::atomic_int clientsConnected{0};
    std::atomic_int serverResultsReceived{0};
    int totalNanos = 0;
    int maxNanos = 0;
    int minNanos = -1;

    serverConnection->onInvoked(
        [](Payload data) {
            return data.asString();
        });

    clientConnection->onConnect([&]
    {
        ++clientsConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientsConnected);
    for (auto i=0; i<receivesExpected; ++i) {
        clientConnection->invoke("Here is just some dumb message",
            [sentTime = std::chrono::high_resolution_clock::now(), &maxNanos, &minNanos, &totalNanos, &serverResultsReceived] (InvokeResultCode, Payload)
        {
            auto resultTime = std::chrono::high_resolution_clock::now();
            auto diff = static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(resultTime - sentTime).count());
            if (diff > maxNanos) {
                maxNanos = diff;
            }
            if (minNanos < 0 || diff < minNanos) {
                minNanos = diff;
            }
            totalNanos += diff;
            ++serverResultsReceived;
        });
        std::this_thread::sleep_for(1ms);
    }

    WAIT_UNTIL_REACHES(receivesExpected, serverResultsReceived, 20);
    printf("Latency - max:%f, min:%f, avg: %f\n", float(maxNanos)/1000000.f, float(minNanos)/1000000.f, float(totalNanos)/1000000.f/receivesExpected);
    EXPECT_LT(maxNanos, 10000000);
    EXPECT_LT(totalNanos / receivesExpected, 1000000);
}

TEST_P(MultiTransmitTest, InvokeLatencyTest)
{
    int receivesExpected = GetParam()._modified ? 10 : 500;
    std::atomic_int clientsConnected{0};
    std::atomic_int serverResultsReceived{0};
    int totalNanos = 0;
    int maxNanos = 0;
    int minNanos = -1;

    serverConnection->onInvoked(
        [](Handle, Payload data) {
            return data.asString();
        });

    clientConnection->onConnect([&]
    {
        ++clientsConnected;
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, clientsConnected);
    for (auto i=0; i<receivesExpected; ++i) {
        clientConnection->invoke("Here is just some dumb message",
            [sentTime = std::chrono::high_resolution_clock::now(), &maxNanos, &minNanos, &totalNanos, &serverResultsReceived] (InvokeResultCode, Payload)
        {
            auto resultTime = std::chrono::high_resolution_clock::now();
            auto diff = static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(resultTime - sentTime).count());
            if (diff > maxNanos) {
                maxNanos = diff;
            }
            if (minNanos < 0 || diff < minNanos) {
                minNanos = diff;
            }
            totalNanos += diff;
            ++serverResultsReceived;
        });
        std::this_thread::sleep_for(1ms);
    }

    WAIT_UNTIL_REACHES(receivesExpected, serverResultsReceived, 20);
    printf("Latency - max:%f, min:%f, avg: %f\n", float(maxNanos)/1000000.f, float(minNanos)/1000000.f, float(totalNanos)/1000000.f/receivesExpected);
    EXPECT_LT(maxNanos, 10000000);
    EXPECT_LT(totalNanos / receivesExpected, 1000000);
}
#endif

TEST_P(TransmitTest, InvokeWithOnResultAndOnInvokeImmediateTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::vector<std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::vector<std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onResult(
        [sleepOnData = _sleepOnData, &serverResponses, &serverResultsReceived](
            Handle, Payload data) {
            Sleep(sleepOnData);
            serverResponses.push_back(data.asString());
            ++serverResultsReceived;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            return command;
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onResult(
        [sleepOnData = _sleepOnData, &clientResponses, &clientResultsReceived](
            Handle, Payload data) {
            Sleep(sleepOnData);
            clientResponses.push_back(data.asString());
            ++clientResultsReceived;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            return command;
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command);
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(command);
        }
        serverConnection->send("bar");
        serverConnection->send("aack");
        serverConnection->send("halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect");
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);
    EXPECT_EQ(requests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(MultiTransmitTest, InvokeWithOnResultAndOnInvokeImmediateTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::map<Handle, std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::map<Handle, std::string> serverResponses;
    std::map<std::pair<Handle, Handle>, std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onResult(
        [sleepOnData = _sleepOnData, &serverResponses, &serverResultsReceived](
            Handle handle, Payload data) {
            Sleep(sleepOnData);
            serverResponses[handle] = data.asString();
            ++serverResultsReceived;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            return command;
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Handle, Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onResult(
        [sleepOnData = _sleepOnData, &clientResponses, &clientResultsReceived](Handle connectionHandle, Handle promiseId, Payload data) {
            Sleep(sleepOnData);
            clientResponses[std::make_pair(connectionHandle, promiseId)] = data.asString() + "-2";
            ++clientResultsReceived;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Handle handle, Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests[handle] = command;
            return command;
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command);
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(connectionHandle, command);
        }
        serverConnection->send(connectionHandle, "bar");
        serverConnection->send(connectionHandle, "aack");
        serverConnection->send(connectionHandle, "halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect");
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(requests, serverRequests);
    Handle index = 0;
    for(auto &response : clientResponses) {
        EXPECT_EQ(response.second, requests[index] + "-2");
        EXPECT_GE(response.first.second, index + 1);
        ++index;
    }

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    index = 0;
    for(auto &response : serverResponses) {
        EXPECT_EQ(response.second, requests[index]);
        EXPECT_EQ(response.first, index + 1);
        ++index;
    }

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(TransmitTest, InvokeWithCallbackAndOnInvokeImmediateTest)
{
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> serverRequests;
    std::vector<std::string> clientRequests;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            return command + "-clientEcho";
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            return command + "-serverEcho";
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command, [&serverResultsReceived, command](InvokeResultCode resultCode, Payload result)
            {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                EXPECT_EQ(command + "-serverEcho", result.asString());
                ++serverResultsReceived;
            });
        }
    });
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(command, [&clientResultsReceived, command](InvokeResultCode resultCode, Payload result)
            {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                EXPECT_EQ(command + "-clientEcho", result.asString());
                ++clientResultsReceived;
            });
        }
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect", [&serverResultsReceived](InvokeResultCode resultCode, Payload result)
    {
        EXPECT_EQ(InvokeResultCode::Good, resultCode);
        EXPECT_EQ("beforeConnect-serverEcho", result.asString());
        ++serverResultsReceived;
    });
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, requests);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, requests);
}

TEST_P(MultiTransmitTest, InvokeWithCallbackAndOnInvokeImmediateTest)
{
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> serverRequests;
    std::vector<std::string> clientRequests;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            return command + "-clientEcho";
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Handle, Payload data) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            return command + "-serverEcho";
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command, [&serverResultsReceived, command](InvokeResultCode resultCode, Payload result)
            {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                EXPECT_EQ(command + "-serverEcho", result.asString());
                ++serverResultsReceived;
            });
        }
    });
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(connectionHandle, command, [&clientResultsReceived, command](InvokeResultCode resultCode, Payload result)
            {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                EXPECT_EQ(command + "-clientEcho", result.asString());
                ++clientResultsReceived;
            });
        }
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect", [&serverResultsReceived](InvokeResultCode resultCode, Payload result)
    {
        EXPECT_EQ(InvokeResultCode::Good, resultCode);
        EXPECT_EQ("beforeConnect-serverEcho", result.asString());
        ++serverResultsReceived;
    });
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, requests);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, requests);
}

TEST_P(TransmitTest, InvokeWithOnResultAndOnInvokeCallbackTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::vector<std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::vector<std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onResult(
        [sleepOnData = _sleepOnData, &serverResponses, &serverResultsReceived](
            Handle, Payload data) {
            Sleep(sleepOnData);
            serverResponses.push_back(data.asString());
            ++serverResultsReceived;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            callback(command);
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onResult(
        [sleepOnData = _sleepOnData, &clientResponses, &clientResultsReceived](
            Handle, Payload data) {
            Sleep(sleepOnData);
            clientResponses.push_back(data.asString());
            ++clientResultsReceived;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command);
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(command);
        }
        serverConnection->send("bar");
        serverConnection->send("aack");
        serverConnection->send("halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect");
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);
    EXPECT_EQ(requests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(MultiTransmitTest, InvokeWithOnResultAndOnInvokeCallbackTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::map<Handle, std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::map<Handle, std::string> serverResponses;
    std::map<std::pair<Handle, Handle>, std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onResult(
        [sleepOnData = _sleepOnData, &serverResponses, &serverResultsReceived](
            Handle handle, Payload data) {
            Sleep(sleepOnData);
            serverResponses[handle] = data.asString();
            ++serverResultsReceived;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            callback(command);
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Handle, Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onResult(
        [sleepOnData = _sleepOnData, &clientResponses, &clientResultsReceived](Handle connectionHandle, Handle promiseId, Payload data) {
            Sleep(sleepOnData);
            clientResponses[std::make_pair(connectionHandle, promiseId)] = data.asString() + "-2";
            ++clientResultsReceived;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Handle handle, Payload data, IServerConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests[handle] = command;
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command);
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(connectionHandle, command);
        }
        serverConnection->send(connectionHandle, "bar");
        serverConnection->send(connectionHandle, "aack");
        serverConnection->send(connectionHandle, "halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect");
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(requests, serverRequests);
    Handle index = 0;
    for(auto &response : clientResponses) {
        EXPECT_EQ(response.second, requests[index] + "-2");
        EXPECT_GE(response.first.second, index + 1);
        ++index;
    }

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    index = 0;
    for(auto &response : serverResponses) {
        EXPECT_EQ(response.second, requests[index]);
        EXPECT_EQ(response.first, index + 1);
        ++index;
    }

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(TransmitTest, InvokeWithCallbackAndOnInvokeCallbackTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::vector<std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::vector<std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            callback(command);
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command, [&serverResponses, &serverResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                serverResponses.push_back(data.asString());
                ++serverResultsReceived;
            });
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(command, [&clientResponses, &clientResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                clientResponses.push_back(data.asString());
                ++clientResultsReceived;
            });
        }
        serverConnection->send("bar");
        serverConnection->send("aack");
        serverConnection->send("halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect", [&serverResponses, &serverResultsReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(InvokeResultCode::Good, resultCode);
            serverResponses.push_back(data.asString());
            ++serverResultsReceived;
        });
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);
    EXPECT_EQ(requests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(MultiTransmitTest, InvokeWithCallbackAndOnInvokeCallbackTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::vector<std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::vector<std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands.push_back(data.asString());
            ++serverSends;
        });

    clientConnection->onInvoked(
        [this, &serverRequests](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            serverRequests.push_back(command);
            callback(command);
        });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Handle, Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onInvoked(
        [this, &clientRequests](Handle, Payload data, IServerConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            clientRequests.push_back(command);
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientConnection->invoke(command, [&serverResponses, &serverResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                serverResponses.push_back(data.asString());
                ++serverResultsReceived;
            });
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(connectionHandle, command, [&clientResponses, &clientResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                clientResponses.push_back(data.asString());
                ++clientResultsReceived;
            });
        }
        serverConnection->send(connectionHandle, "bar");
        serverConnection->send(connectionHandle, "aack");
        serverConnection->send(connectionHandle, "halt");
    });

    clientConnection->connect();
    clientConnection->invoke("beforeConnect", [&serverResponses, &serverResultsReceived](InvokeResultCode resultCode, Payload data) {
        EXPECT_EQ(InvokeResultCode::Good, resultCode);
        serverResponses.push_back(data.asString());
        ++serverResultsReceived;
    });
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);
    EXPECT_EQ(requests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(TransmitTest, InvokeWithCallbackAndOnInvokePromiseIdTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::map<Handle, std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::map<Handle, std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived([sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
        Sleep(sleepOnData);
        serverCommands.push_back(data.asString());
        ++serverSends;
    });

    clientConnection->onInvoked([this, &serverRequests](Handle connectionHandle, Handle promiseId, Payload data) {
        Sleep(_sleepOnData);
        auto command = data.asString();
        serverRequests.push_back(command);
        clientConnection->sendResult(connectionHandle, promiseId, command);
    });

    clientConnection->onResult([&serverResponses, &serverResultsReceived](Handle promiseId, Payload data) {
        serverResponses[promiseId] = data.asString();
        ++serverResultsReceived;        
    });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onInvoked(
        [this](Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientRequests[clientConnection->invoke(command)] = command;
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(command, [&clientResponses, &clientResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                clientResponses.push_back(data.asString());
                ++clientResultsReceived;
            });
        }
        serverConnection->send("bar");
        serverConnection->send("aack");
        serverConnection->send("halt");
    });

    clientConnection->connect();
    clientRequests[clientConnection->invoke("beforeConnect")] = "beforeConnect";
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(MultiTransmitTest, InvokeWithCallbackAndOnInvokePromiseIdTest)
{
    std::atomic_int serverSends{0};
    std::atomic_int clientSends{0};
    std::atomic_int serverResultsReceived{0};
    std::atomic_int clientResultsReceived{0};
    std::vector<std::string> clientCommands;
    std::vector<std::string> serverCommands;
    std::map<Handle, std::string> clientRequests;
    std::vector<std::string> serverRequests;
    std::map<Handle, std::string> serverResponses;
    std::vector<std::string> clientResponses;
    std::vector<std::string> requests = {"start", "stop", "skip", "returnToSender", "quit"};

    clientConnection->onReceived([sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
        Sleep(sleepOnData);
        serverCommands.push_back(data.asString());
        ++serverSends;
    });

    clientConnection->onInvoked([this, &serverRequests](Handle connectionHandle, Handle promiseId, Payload data) {
        Sleep(_sleepOnData);
        auto command = data.asString();
        serverRequests.push_back(command);
        clientConnection->sendResult(connectionHandle, promiseId, command);
    });

    clientConnection->onResult([&serverResponses, &serverResultsReceived](Handle promiseId, Payload data) {
        serverResponses[promiseId] = data.asString();
        ++serverResultsReceived;        
    });

    serverConnection->onReceived(
        [sleepOnData = _sleepOnData, &clientCommands, &clientSends](Handle, Payload data) {
            Sleep(sleepOnData);
            clientCommands.push_back(data.asString());
            ++clientSends;
        });

    serverConnection->onInvoked(
        [this](Handle, Payload data, IConnection::ResultCallback callback) {
            Sleep(_sleepOnData);
            auto command = data.asString();
            callback(command);
        });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->send("aack");
        for(auto &command : requests) {
            clientRequests[clientConnection->invoke(command)] = command;
        }
        clientConnection->send("bar");
    });
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        for(auto &command : requests) {
            serverConnection->invoke(connectionHandle, command, [&clientResponses, &clientResultsReceived](InvokeResultCode resultCode, Payload data) {
                EXPECT_EQ(InvokeResultCode::Good, resultCode);
                clientResponses.push_back(data.asString());
                ++clientResultsReceived;
            });
        }
        serverConnection->send(connectionHandle, "bar");
        serverConnection->send(connectionHandle, "aack");
        serverConnection->send(connectionHandle, "halt");
    });

    clientConnection->connect();
    clientRequests[clientConnection->invoke("beforeConnect")] = "beforeConnect";
    serverConnection->connect();

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), clientResultsReceived, 20);
    EXPECT_EQ(serverRequests, clientResponses);
    EXPECT_EQ(requests, clientResponses);

    requests.insert(requests.begin(), "beforeConnect");

    WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
    EXPECT_EQ(clientRequests, serverResponses);

    WAIT_UNTIL_REACHES(2, clientSends, 30);
    WAIT_UNTIL_REACHES(3, serverSends, 30);
    ASSERT_EQ(2, static_cast<int>(clientCommands.size()));
    EXPECT_EQ("aack", clientCommands[0]);
    ASSERT_EQ(3, static_cast<int>(serverCommands.size()));
    EXPECT_EQ("bar", serverCommands[0]);
}

TEST_P(TransmitTest, InvokeClientDisconnectedRemotelyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    clientConnection->onInvoked([this](Handle, Handle, Payload) {
        Sleep(_sleepOnData);
        clientConnection->disconnect();
    });

    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        serverConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived, 20);
}

TEST_P(MultiTransmitTest, InvokeClientDisconnectedRemotelyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    clientConnection->onInvoked([this](Handle, Handle, Payload) {
        Sleep(_sleepOnData);
        clientConnection->disconnect();
    });

    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        serverConnection->invoke(connectionHandle, "test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived, 20);
}

TEST_P(TransmitTest, InvokeServerDisconnectedRemotelyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    serverConnection->onInvoked([this](Handle, Handle, Payload) {
        Sleep(_sleepOnData);
        serverConnection->disconnect();
    });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived, 20);
}

TEST_P(MultiTransmitTest, InvokeServerDisconnectedRemotelyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    serverConnection->onInvoked([this](Handle, Handle, Payload) {
        Sleep(_sleepOnData);
        serverConnection->disconnect();
    });

    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived, 20);
}

TEST_P(TransmitTest, InvokeClientDisconnectedLocallyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::LocalDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
        clientConnection->disconnect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
}

TEST_P(MultiTransmitTest, InvokeClientDisconnectedLocallyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    clientConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::LocalDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
        clientConnection->disconnect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
}

TEST_P(TransmitTest, InvokeServerDisconnectedLocallyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    serverConnection->onConnect([&]
    {
        Sleep(_sleepOnConnect);
        serverConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::LocalDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
        serverConnection->disconnect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
}

TEST_P(MultiTransmitTest, InvokeServerDisconnectedLocallyTest)
{
    std::atomic_int invokeResponseReceived = 0;
    serverConnection->onConnect([&](Handle connectionHandle)
    {
        Sleep(_sleepOnConnect);
        serverConnection->invoke(connectionHandle, "test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload data) {
            EXPECT_EQ(resultCode, InvokeResultCode::LocalDisconnect);
            EXPECT_EQ(static_cast<int>(data.size()), 0);
            invokeResponseReceived = 1;
        });
        serverConnection->disconnect();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
}

TEST_P(TransmitTest, InvokeClientExpiredLambdaTest)
{
    std::atomic_int invokeResponseReceived = 0;
    std::atomic_int onInvokedCalled = 0;
    std::atomic_int onInvokedCompleted = 0;

    clientConnection->onInvoked(
            [&onInvokedCalled, &onInvokedCompleted](Payload, IConnection::ResultCallback callback) {
                onInvokedCalled = 1;
                std::thread([callback = std::move(callback), &onInvokedCompleted]() {
                    Sleep(100ms);
                    callback("won't happen");
                    onInvokedCompleted = 1;
                }).detach();
            });

    serverConnection->onConnect([&]{
        Sleep(_sleepOnConnect);
        serverConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            invokeResponseReceived = 1;
        });
        WAIT_UNTIL_REACHES(1, onInvokedCalled);
        clientConnection.reset();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
    WAIT_UNTIL_REACHES(1, onInvokedCompleted);
}

TEST_P(MultiTransmitTest, InvokeClientExpiredLambdaTest)
{
    std::atomic_int invokeResponseReceived = 0;
    std::atomic_int onInvokedCalled = 0;
    std::atomic_int onInvokedCompleted = 0;

    clientConnection->onInvoked(
            [&onInvokedCalled, &onInvokedCompleted](Payload, IConnection::ResultCallback callback) {
                onInvokedCalled = 1;
                std::thread([callback = std::move(callback), &onInvokedCompleted]() {
                    Sleep(100ms);
                    callback("won't happen");
                    onInvokedCompleted = 1;
                }).detach();
            });

    serverConnection->onConnect([&](Handle connectionHandle){
        Sleep(_sleepOnConnect);
        serverConnection->invoke(connectionHandle, "test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            invokeResponseReceived = 1;
        });
        WAIT_UNTIL_REACHES(1, onInvokedCalled);
        clientConnection.reset();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
    WAIT_UNTIL_REACHES(1, onInvokedCompleted);
}

TEST_P(TransmitTest, InvokeServerExpiredLambdaTest)
{
    std::atomic_int invokeResponseReceived = 0;
    std::atomic_int onInvokedCalled = 0;
    std::atomic_int onInvokedCompleted = 0;

    serverConnection->onInvoked(
            [&onInvokedCalled, &onInvokedCompleted](Payload, IConnection::ResultCallback callback) {
                onInvokedCalled = 1;
                std::thread([callback = std::move(callback), &onInvokedCompleted]() {
                    Sleep(100ms);
                    callback("won't happen");
                    onInvokedCompleted = 1;
                }).detach();
            });

    clientConnection->onConnect([&]{
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            invokeResponseReceived = 1;
        });
        WAIT_UNTIL_REACHES(1, onInvokedCalled);
        serverConnection.reset();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
    WAIT_UNTIL_REACHES(1, onInvokedCompleted);
}

TEST_P(MultiTransmitTest, InvokeServerExpiredLambdaTest)
{
    std::atomic_int invokeResponseReceived = 0;
    std::atomic_int onInvokedCalled = 0;
    std::atomic_int onInvokedCompleted = 0;

    serverConnection->onInvoked(
            [&onInvokedCalled, &onInvokedCompleted](Handle, Payload, IConnection::ResultCallback callback) {
                onInvokedCalled = 1;
                std::thread([callback = std::move(callback), &onInvokedCompleted]() {
                    Sleep(100ms);
                    callback("won't happen");
                    onInvokedCompleted = 1;
                }).detach();
            });

    clientConnection->onConnect([&]{
        Sleep(_sleepOnConnect);
        clientConnection->invoke("test", [&invokeResponseReceived](InvokeResultCode resultCode, Payload) {
            EXPECT_EQ(resultCode, InvokeResultCode::RemoteDisconnect);
            invokeResponseReceived = 1;
        });
        WAIT_UNTIL_REACHES(1, onInvokedCalled);
        serverConnection.reset();
    });

    serverConnection->connect();
    clientConnection->connect();

    WAIT_UNTIL_REACHES(1, invokeResponseReceived);
    WAIT_UNTIL_REACHES(1, onInvokedCompleted);
}

TEST_P(MultiTransmitTest, BroadcastTest)
{
    std::atomic_int serverSends{0};
    std::string serverCommands;

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &serverCommands, &serverSends](Payload data) {
            Sleep(sleepOnData);
            serverCommands = data.asString();
            ++serverSends;
        }
    );

    serverConnection->onConnect([&](Handle) {
        Sleep(_sleepOnConnect);
        serverConnection->broadcast("test");
    });
    clientConnection->connect();
    serverConnection->connect();

    WAIT_UNTIL_REACHES(1, serverSends, 20);
    EXPECT_EQ(serverCommands, "test");
}

TEST_P(MultiConnectIPCTest, ClientServerTest)
{
    std::atomic_int gotShort{0};
    std::atomic_int gotDisconnect{0};

    auto clientConnection2 = std::shared_ptr<IConnection>(ConnectionFactory::newClientConnection(s_EndPoint));
    HookClient(clientConnection2.get());
    auto clientConnection3 = std::shared_ptr<IConnection>(ConnectionFactory::newClientConnection(s_EndPoint));
    HookClient(clientConnection3.get());

    std::vector<uint8_t> shortMessage(1000);

    shortMessage[shortMessage.size() - 1] = '\0';

    for(size_t i = 0; i < shortMessage.size() - 1; ++i) {
        shortMessage[i] = static_cast<char>(rand() % 28) + 97;
    }

    serverConnection->onConnect(
        [&](Handle handle) { serverConnection->send(handle, shortMessage); });
    serverConnection->onDisconnect([&](Handle) { ++gotDisconnect; });

    clientConnection->onReceived(
        [sleepOnData = _sleepOnData, &shortMessage, &gotShort](Payload data) {
            Sleep(sleepOnData);
            if(data.size() == shortMessage.size()) {
                EXPECT_EQ(data, shortMessage);
                ++gotShort;
            }
        });

    clientConnection2->onReceived(
        [sleepOnData = _sleepOnData, &shortMessage, &gotShort](Payload data) {
            Sleep(sleepOnData);
            if(data.size() == shortMessage.size()) {
                EXPECT_EQ(data, shortMessage);
                ++gotShort;
            }
        });

    clientConnection3->onReceived(
        [sleepOnData = _sleepOnData, &shortMessage, &gotShort](Payload data) {
            Sleep(sleepOnData);
            if(data.size() == shortMessage.size()) {
                EXPECT_EQ(data, shortMessage);
                ++gotShort;
            }
        });

    serverConnection->connect();
    clientConnection->connect();
    clientConnection2->connect();
    clientConnection3->connect();

    WAIT_UNTIL_REACHES(3, gotShort, 30);

    serverConnection->broadcast(shortMessage);
    WAIT_UNTIL_REACHES(6, gotShort, 30);

    clientConnection->disconnect();
    clientConnection2->disconnect();
    clientConnection3->disconnect();

    WAIT_UNTIL_REACHES(3, gotDisconnect, 2);
    WAIT_UNTIL_EQ(0, [this] { return serverConnection->activeConnections(); });
}

TEST_P(MultiConnectIPCTest, MultiInvokeTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientDisconnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::vector<std::string> requests{"one", "two", "three", "four", "five"};
    std::map<Handle, std::string> clientRequests;
    std::map<Handle, std::vector<std::string>> clientCommands;
    std::set<Handle> clientHandles;

    serverConnection->onConnect([this, &clientConnectionCount, &clientHandles](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        clientHandles.emplace(connectionHandle);
        ++clientConnectionCount;
    });
    serverConnection->onDisconnect([this, &clientDisconnectionCount](Handle) {
        Sleep(_sleepOnDisconnect);
        ++clientDisconnectionCount;
    });
    serverConnection->onInvoked([this, &clientRequests](Handle handle, Payload data) {
        Sleep(_sleepOnData);
        auto command = data.asString();
        clientRequests[handle] = command;
        return command;
    });
    serverConnection->connect();

    {
        DispatchQueue queue;
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &requests, &clientCompletedCount] {
                std::atomic_int serverResultsReceived{0};
                std::vector<Handle> promises;
                std::map<Handle, std::string> serverResponses;
                auto myRequests = std::vector<std::string>(requests);
                std::shuffle(myRequests.begin(),
                    myRequests.end(),
                    std::default_random_engine{static_cast<unsigned int>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()))});
                auto client = MakeClient();

                client->onResult([&serverResponses, &serverResultsReceived](Handle handle, Payload data) {
                    serverResponses[handle] = data.asString();
                    ++serverResultsReceived;
                });
                client->connect();
                promises.reserve(myRequests.size());
                for(auto &request : myRequests) {
                    promises.push_back(client->invoke(request));
                }
                WAIT_UNTIL_REACHES(static_cast<int>(myRequests.size()), serverResultsReceived, 20);
                ++clientCompletedCount;
            });
        }
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientDisconnectionCount, 30);
    EXPECT_EQ(numberOfConnections, clientConnectionCount);
    EXPECT_EQ(numberOfConnections, clientCompletedCount);
}

TEST_P(MultiConnectIPCTest, MultiInvokeCallbackTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientDisconnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::vector<std::string> requests{"one", "two", "three", "four", "five"};
    std::map<Handle, std::string> clientRequests;
    std::map<Handle, std::vector<std::string>> clientCommands;
    std::set<Handle> clientHandles;

    serverConnection->onConnect([this, &clientConnectionCount, &clientHandles](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        clientHandles.emplace(connectionHandle);
        ++clientConnectionCount;
    });
    serverConnection->onDisconnect([this, &clientDisconnectionCount](Handle) {
        Sleep(_sleepOnDisconnect);
        ++clientDisconnectionCount;
    });
    serverConnection->onInvoked([this, &clientRequests](Handle handle, Payload data) {
        Sleep(_sleepOnData);
        auto command = data.asString();
        clientRequests[handle] = command;
        return command + "-serverEcho";
    });
    serverConnection->connect();

    {
        DispatchQueue queue;
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &requests, &clientCompletedCount] {
                std::atomic_int serverResultsReceived{0};
                auto myRequests = std::vector<std::string>(requests);
                std::shuffle(myRequests.begin(),
                    myRequests.end(),
                    std::default_random_engine{static_cast<unsigned int>(
                        std::hash<std::thread::id>{}(std::this_thread::get_id()))});
                auto client = MakeClient();

                client->connect();
                for(auto &request : myRequests) {
                    client->invoke(request, [request, &serverResultsReceived](InvokeResultCode resultCode, Payload result) {
                        EXPECT_EQ(InvokeResultCode::Good, resultCode);
                        ++serverResultsReceived;
                        EXPECT_EQ(request + "-serverEcho", result.asString());
                    });
                }
                WAIT_UNTIL_REACHES(static_cast<int>(requests.size()), serverResultsReceived, 20);
                ++clientCompletedCount;
            });
        }
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientDisconnectionCount, 30);
}

TEST_P(MultiConnectIPCTest, MultiInvokeClientDisconnectedRemoteTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientDisconnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::atomic_int invokeResponseCount{0};

    serverConnection->onConnect([this, &clientConnectionCount, &invokeResponseCount](Handle connectionHandle) {
        ++clientConnectionCount;
        Sleep(_sleepOnConnect);
        serverConnection->send(connectionHandle, "kill");
        serverConnection->invoke(connectionHandle, "test", [&invokeResponseCount](InvokeResultCode result, Payload data) {
            EXPECT_EQ(InvokeResultCode::RemoteDisconnect, result);
            EXPECT_EQ(true, data.empty());
            ++invokeResponseCount;
        });
    });
    serverConnection->onDisconnect([this, &clientDisconnectionCount](Handle) {
        Sleep(_sleepOnDisconnect);
        ++clientDisconnectionCount;
    });
    serverConnection->connect();

    {
        DispatchQueue queue;
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([&] {
                std::atomic_int disconnects{0};
                auto client = MakeClient();
                client->onReceived([&](Payload) {
                    Sleep(_sleepOnData);
                    client->disconnect();
                    ++disconnects;
                });
                client->connect();
                WAIT_UNTIL_REACHES(1, disconnects, 10);
                ++clientCompletedCount;
            });
        }
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientDisconnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientCompletedCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, invokeResponseCount, 30);
}

TEST_P(MultiConnectIPCTest, MultiInvokeClientDestroyedTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientDisconnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::atomic_int invokeResponseCount{0};

    serverConnection->onConnect([this, &clientConnectionCount, &invokeResponseCount](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        serverConnection->send(connectionHandle, "kill");
        serverConnection->invoke(connectionHandle, "test", [&invokeResponseCount](InvokeResultCode result, Payload data) {
            EXPECT_EQ(InvokeResultCode::RemoteDisconnect, result);
            EXPECT_EQ(true, data.empty());
            ++invokeResponseCount;
        });
        ++clientConnectionCount;
    });
    serverConnection->onDisconnect([this, &clientDisconnectionCount](Handle) {
        Sleep(_sleepOnDisconnect);
        ++clientDisconnectionCount;
    });
    serverConnection->connect();

    {
        DispatchQueue queue;
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &clientCompletedCount] {
                std::atomic_int disconnects{0};
                auto client = MakeClient();
                client->onReceived([&](Payload) {
                    Sleep(_sleepOnData);
                    ++disconnects;
                });
                client->connect();
                WAIT_UNTIL_REACHES(1, disconnects, 5);
                ++clientCompletedCount;
            });
        }
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientDisconnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientCompletedCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, invokeResponseCount, 30);
}

TEST_P(MultiConnectIPCTest, MultiInvokeServerDisconnectedRemoteTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::atomic_int gotKill{0};

    serverConnection->onConnect([&](Handle) {
        Sleep(_sleepOnConnect);
        ++clientConnectionCount;
    });
    serverConnection->onReceived([&](Handle, Payload) {
        Sleep(_sleepOnData);
        ++gotKill;
    });
    serverConnection->connect();

    {
        DispatchQueue queue(numberOfConnections);
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &clientCompletedCount] {
                std::atomic_int disconnects{0};
                std::atomic_int invokesCompleted{0};
                auto client = MakeClient();
                client->onConnect([&] {
                    client->send("kill");
                    client->invoke("test", [&](InvokeResultCode result, Payload data) {
                        EXPECT_EQ(InvokeResultCode::RemoteDisconnect, result);
                        EXPECT_EQ(true, data.empty());
                        ++invokesCompleted;
                    });
                    Sleep(_sleepOnConnect);
                });
                client->onDisconnect([&] {
                    ++disconnects;
                });
                client->connect();
                WAIT_UNTIL_REACHES(1, disconnects, 30);
                if (!invokesCompleted) {
                    client->disconnect();
                }
                WAIT_UNTIL_REACHES(1, invokesCompleted);
                ++clientCompletedCount;
            });
        }
        WAIT_UNTIL_REACHES(numberOfConnections, gotKill, 30);
        serverConnection->disconnect();
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount, 30);
    WAIT_UNTIL_REACHES(numberOfConnections, clientCompletedCount, 30);
}

TEST_P(MultiConnectIPCTest, MultiInvokeServerDisconnectedLocalTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::atomic_int invokesCompleted{0};

    serverConnection->onConnect([&](Handle connectionHandle) {
        Sleep(_sleepOnConnect);
        ++clientConnectionCount;
        serverConnection->invoke(connectionHandle, "test", [&](InvokeResultCode result, Payload data) {
            EXPECT_EQ(InvokeResultCode::LocalDisconnect, result);
            EXPECT_EQ(true, data.empty());
            ++invokesCompleted;
        });
    });
    serverConnection->connect();

    {
        DispatchQueue queue(numberOfConnections);
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &clientCompletedCount] {
                std::atomic_int disconnects{0};
                auto client = MakeClient();
                client->onDisconnect([&] {
                    ++disconnects;
                });
                client->connect();
                WAIT_UNTIL_REACHES(1, disconnects, 30);
                ++clientCompletedCount;
            });
        }
        WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount, 30);
        serverConnection->disconnect();
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientCompletedCount, 30);
}

TEST_P(MultiConnectIPCTest, MultiInvokeServerDestroyedTest)
{
    const int numberOfConnections = 20;
    std::atomic_int clientConnectionCount{0};
    std::atomic_int clientCompletedCount{0};
    std::atomic_int gotKill{0};

    serverConnection->onConnect([this, &clientConnectionCount](Handle) {
        Sleep(_sleepOnConnect);
        ++clientConnectionCount;
    });
    serverConnection->onReceived([&](Handle, Payload) {
        Sleep(_sleepOnData);
        ++gotKill;
    });
    serverConnection->connect();

    {
        DispatchQueue queue(numberOfConnections);
        for(auto i = 0; i < numberOfConnections; ++i) {
            queue.Dispatch([this, &clientCompletedCount] {
                std::atomic_int disconnects{0};
                std::atomic_int invokesCompleted{0};
                auto client = MakeClient();
                client->onConnect([&] {
                    client->send("kill");
                    client->invoke("test", [&](InvokeResultCode result, Payload data) {
                        EXPECT_EQ(InvokeResultCode::RemoteDisconnect, result);
                        EXPECT_EQ(true, data.empty());
                        ++invokesCompleted;
                    });
                    Sleep(_sleepOnConnect);
                });
                client->onDisconnect([&] {
                    ++disconnects;
                });
                client->connect();
                WAIT_UNTIL_REACHES(1, disconnects, 30);
                if (!invokesCompleted) {
                    client->disconnect();
                }
                WAIT_UNTIL_REACHES(1, invokesCompleted);
                ++clientCompletedCount;
            });
        }
        WAIT_UNTIL_REACHES(numberOfConnections, gotKill, 30);
        serverConnection.reset();
        queue.Wait();
    }
    WAIT_UNTIL_REACHES(numberOfConnections, clientConnectionCount);
    WAIT_UNTIL_REACHES(numberOfConnections, clientCompletedCount);
}

auto values = ::testing::Values(
    TestSettings{}.SleepOnConnect(0ms).SleepOnDisconnect(0ms).SleepOnData(0ms).SleepOnLog(0ms),
    TestSettings{}.SleepOnConnect(1ms).SleepOnDisconnect(1ms).SleepOnData(1ms).SleepOnLog(1ms),
    TestSettings{}.LogLevel(LogLevel::Debug).WriteLogs(true),
    TestSettings{}.SleepOnConnect().SleepOnDisconnect().SleepOnData().SleepOnLog().LogLevel(LogLevel::Debug)
);

#if DO_EXPLICIT_CHECKS
INSTANTIATE_TEST_SUITE_P(Explicit, ConnectDisconnectTest, values);
INSTANTIATE_TEST_SUITE_P(Explicit, TransmitTest, values);
INSTANTIATE_TEST_SUITE_P(Explicit, MultiConnectDisconnectTest, values);
INSTANTIATE_TEST_SUITE_P(Explicit, MultiTransmitTest, values);
INSTANTIATE_TEST_SUITE_P(Explicit, MultiConnectIPCTest, values);
#endif

#if NUMBER_OF_REPEATS
std::vector<TestSettings> NormalSettings(NUMBER_OF_REPEATS);
INSTANTIATE_TEST_SUITE_P(Repeats, ConnectDisconnectTest, ::testing::ValuesIn(NormalSettings));
INSTANTIATE_TEST_SUITE_P(Repeats, TransmitTest, ::testing::ValuesIn(NormalSettings));
INSTANTIATE_TEST_SUITE_P(Repeats, MultiConnectDisconnectTest, ::testing::ValuesIn(NormalSettings));
INSTANTIATE_TEST_SUITE_P(Repeats, MultiTransmitTest, ::testing::ValuesIn(NormalSettings));
INSTANTIATE_TEST_SUITE_P(Repeats, MultiConnectIPCTest, ::testing::ValuesIn(NormalSettings));
#endif
