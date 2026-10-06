#pragma once

#include <cstdarg>

namespace rr {

enum class LogLevel { Debug, Info, Warn, Error };

void LogSetMinLevel(LogLevel level);
void LogWrite(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#define RR_DEBUG(...) ::rr::LogWrite(::rr::LogLevel::Debug, __VA_ARGS__)
#define RR_INFO(...) ::rr::LogWrite(::rr::LogLevel::Info, __VA_ARGS__)
#define RR_WARN(...) ::rr::LogWrite(::rr::LogLevel::Warn, __VA_ARGS__)
#define RR_ERROR(...) ::rr::LogWrite(::rr::LogLevel::Error, __VA_ARGS__)

}  // namespace rr
