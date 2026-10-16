#pragma once

#include <cstddef>

namespace cs2_sdk::log {
enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

// Creates per-plugin console and daily-file sinks.
bool Init(const char* baseDir, const char* pluginName, char* error, size_t maxlen);
// Flushes and unregisters only this plugin's logger.
void Close();
// Formats existing printf-style diagnostics for the shared logger.
void Write(Level level, const char* format, ...);
} // namespace cs2_sdk::log
