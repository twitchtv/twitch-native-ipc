// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

// ReSharper disable CppClangTidyBugproneMacroParentheses
#pragma once

#define DELETE_COPY_AND_MOVE_CONSTRUCTORS(ClassName)                                               \
    ClassName(const ClassName &) = delete;                                                         \
    ClassName(ClassName &&) = delete;                                                              \
    ClassName &operator=(const ClassName &) = delete;                                              \
    ClassName &operator=(ClassName &&) = delete
