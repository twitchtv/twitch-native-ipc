#pragma once

#define LOG(handle, level, message)                                                                \
    do {                                                                                           \
        if(_logHandler && (level) >= _logLevel)                                                    \
            handleLog(handle, level, message);                                                     \
    } while(false)
#define LOG_DEBUG(handle, message) LOG(handle, LogLevel::Debug, message)
#define LOG_INFO(handle, message) LOG(handle, LogLevel::Info, message)
#define LOG_WARNING(handle, message) LOG(handle, LogLevel::Warning, message)
#define LOG_ERROR(handle, message) LOG(handle, LogLevel::Error, message)

#define LOG_WITH_ERROR_CODE(handle, level, message, error_code)                                    \
    do {                                                                                           \
        if(_logHandler && (level) >= _logLevel)                                                    \
            handleLog(handle,                                                                      \
                level,                                                                             \
                message + std::string(" - ") +                                                     \
                    std::string(uv_err_name(static_cast<int>(error_code))) + ": " +                \
                    uv_strerror(static_cast<int>(error_code)));                                    \
    } while(false)
#define LOG_WARNING_WITH_ERROR_CODE(handle, message, error_code)                                   \
    LOG_WITH_ERROR_CODE(handle, LogLevel::Warning, message, error_code)
#define LOG_ERROR_WITH_ERROR_CODE(handle, message, error_code)                                     \
    LOG_WITH_ERROR_CODE(handle, LogLevel::Error, message, error_code)
