// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include <IConnection.h>

namespace Twitch::IPC {
struct MessageHeader {
    Handle handle;
    uint32_t bodySize;
};
} // namespace Twitch::IPC
