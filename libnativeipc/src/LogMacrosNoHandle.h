// Copyright Twitch Interactive, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT

#pragma once

#define LOG(level, message)                                                                        \
    do {                                                                                           \
        if(_logHandler && (level) >= _logLevel)                                                    \
            handleLog(0, level, message);                                                          \
    } while(false)
#define LOG_DEBUG(message) LOG(LogLevel::Debug, message)
#define LOG_INFO(message) LOG(LogLevel::Info, message)
#define LOG_WARNING(message) LOG(LogLevel::Warning, message)
#define LOG_ERROR(message) LOG(LogLevel::Error, message)

#define LOG_WITH_ERROR_CODE(level, message, error_code)                                            \
    do {                                                                                           \
        if(_logHandler && (level) >= _logLevel)                                                    \
            handleLog(0, level, message + std::string(" - ") +                                     \
                                std::string(uv_err_name(error_code)) + ": " +                      \
                                uv_strerror(error_code));                                          \
    } while(false)
#define LOG_WARNING_WITH_ERROR_CODE(message, error_code) LOG_WITH_ERROR_CODE(LogLevel::Warning, message, error_code)
#define LOG_ERROR_WITH_ERROR_CODE(message, error_code) LOG_WITH_ERROR_CODE(LogLevel::Error, message, error_code)
