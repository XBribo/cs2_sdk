#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cs2_sdk::log {
namespace {
std::shared_ptr<spdlog::logger> g_logger;
} // namespace

bool Init(const char* baseDir, const char* pluginName, char* error, size_t maxlen)
{
    if (g_logger || !baseDir || !baseDir[0] || !pluginName || !pluginName[0])
    {
        if (error && maxlen)
            std::snprintf(error, maxlen, "%s", g_logger ? "logger is already initialized" : "invalid logger path or name");
        return false;
    }
    std::shared_ptr<spdlog::logger> logger;
    bool registered = false;
    try
    {
        const std::string name(pluginName);
        const auto directory = std::filesystem::path(baseDir) / "addons" / name / "logs";
        std::filesystem::create_directories(directory);
        std::vector<spdlog::sink_ptr> sinks;
        sinks.emplace_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
        // Rotate at local midnight and retain the latest 31 daily files.
        sinks.emplace_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>((directory / (name + ".log")).string(), 0, 0, false, 31));
        sinks[0]->set_pattern("%^[%T] [%l] %n: %v%$");
        sinks[1]->set_pattern("[%Y-%m-%d %T] [%l] %n: %v");
        logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::register_logger(logger);
        registered = true;
        spdlog::cfg::load_env_levels();
        g_logger = std::move(logger);
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error && maxlen) std::snprintf(error, maxlen, "cannot initialize %s logger: %s", pluginName, exception.what());
        if (registered) spdlog::drop(logger->name());
        return false;
    }
}

void Close()
{
    if (!g_logger) return;
    g_logger->flush();
    if (spdlog::get(g_logger->name()) == g_logger) spdlog::drop(g_logger->name());
    g_logger.reset();
}

void Write(Level level, const char* format, ...)
{
    const auto severity = level == Level::Debug   ? spdlog::level::debug
                          : level == Level::Warn  ? spdlog::level::warn
                          : level == Level::Error ? spdlog::level::err
                                                  : spdlog::level::info;
    if (g_logger && !g_logger->should_log(severity)) return;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (size < 0)
    {
        va_end(args);
        return;
    }
    std::vector<char> message(static_cast<size_t>(size) + 1);
    std::vsnprintf(message.data(), message.size(), format, args);
    va_end(args);
    size_t length = static_cast<size_t>(size);
    while (length && (message[length - 1] == '\n' || message[length - 1] == '\r'))
        --length;
    if (g_logger) g_logger->log(severity, spdlog::string_view_t(message.data(), length));
    else
        std::fprintf(stderr, "%s\n", message.data());
}
} // namespace cs2_sdk::log
