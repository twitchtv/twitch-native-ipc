#pragma once

#include <IConnection.h>

namespace Twitch::IPC {
struct MessageHeader {
    Handle handle;
    uint32_t bodySize;
};
} // namespace Twitch::IPC
