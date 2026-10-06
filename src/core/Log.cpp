#include "core/Log.h"

#include <cstdio>
#include <mutex>

namespace rr {

namespace {
LogLevel g_minLevel = LogLevel::Info;
std::mutex g_logMutex;
FILE* g_logFile = nullptr;

const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}
}  // namespace

void LogSetMinLevel(LogLevel level) { g_minLevel = level; }

void LogWrite(LogLevel level, const char* fmt, ...) {
    if (level < g_minLevel) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile) g_logFile = std::fopen("RockRaiders.log", "w");

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::fprintf(stderr, "[%s] %s\n", LevelTag(level), buffer);
    if (g_logFile) {
        std::fprintf(g_logFile, "[%s] %s\n", LevelTag(level), buffer);
        std::fflush(g_logFile);
    }
}

}  // namespace rr
