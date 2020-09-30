#include <thread>
#include "ConnectionBase.h"

using namespace Twitch::IPC;

#if _WIN32
#define strcasecmp _stricmp
#endif

NATIVEIPC_LIBSPEC LogLevel Twitch::IPC::fromString(const char *value)
{
    if(!strcasecmp(value, "debug"))
        return LogLevel::Debug;
    if(!strcasecmp(value, "info"))
        return LogLevel::Info;
    if(!strcasecmp(value, "warning"))
        return LogLevel::Warning;
    if(!strcasecmp(value, "error"))
        return LogLevel::Error;
    return LogLevel::None;
}

NATIVEIPC_LIBSPEC const char *Twitch::IPC::toString(LogLevel value)
{
    switch(value) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::None:
        return "NONE";
    }
    return "UNKNOWN";
}

ConnectionBase::ConnectionBase(
    std::shared_ptr<ConnectionFactory::Factory> factory, std::string endpoint)
    : _factory(std::move(factory))
    , _lambdaShield(std::make_shared<int>(1))
    , _endpoint(std::move(endpoint))
{
}

Handle ConnectionBase::getNextHandle()
{
    auto result = ++_lastHandle;
    if(_lastHandle >= ResponseFlag) {
        std::lock_guard guard(_rolloverMutex);
        if(_lastHandle >= ResponseFlag) {
            _lastHandle = 0;
        }
        result = ++_lastHandle;
    }
    return result;
}

void ConnectionBase::clearLambdaShield()
{
    if (_lambdaShield) {
        std::weak_ptr<int> weakMarker(_lambdaShield);
        _lambdaShield.reset();
        while(true) {
            {
                auto locked = weakMarker.lock();
                if (!locked) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
