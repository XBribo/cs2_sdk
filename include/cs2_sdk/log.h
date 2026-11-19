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

// Creates sinks; failed or repeated Init leaves the current logger intact.
bool Init(const char* baseDir, const char* pluginName, char* error, size_t maxlen);
// Flushes and unregisters this plugin's logger.
void Close();
// Formats printf-style diagnostics for the plugin logger.
void Write(Level level, const char* format, ...);
} // namespace cs2_sdk::log
