#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace cs2_sdk::log {
namespace {
std::shared_ptr<spdlog::logger> g_logger;
std::string g_name;
} // namespace

bool Init(const char* baseDir, const char* pluginName, char* error, size_t maxlen)
{
    try
    {
        g_name = pluginName;
        const auto directory = std::filesystem::path(baseDir) / "addons" / g_name / "logs";
        std::filesystem::create_directories(directory);
        std::vector<spdlog::sink_ptr> sinks;
        sinks.emplace_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
        // Rotate at local midnight and retain the latest 31 daily files.
        sinks.emplace_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>((directory / (g_name + ".log")).string(), 0, 0, false, 31));
        sinks[0]->set_pattern("%^[%T] [%l] %n: %v%$");
        sinks[1]->set_pattern("[%Y-%m-%d %T] [%l] %n: %v");
        g_logger = std::make_shared<spdlog::logger>(g_name, sinks.begin(), sinks.end());
        spdlog::register_logger(g_logger);
        g_logger->set_level(spdlog::level::info);
        g_logger->flush_on(spdlog::level::info);
        spdlog::cfg::load_env_levels();
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error && maxlen) std::snprintf(error, maxlen, "cannot initialize %s logger: %s", pluginName, exception.what());
        Close();
        return false;
    }
}

void Close()
{
    if (g_logger) g_logger->flush();
    if (!g_name.empty()) spdlog::drop(g_name);
    g_logger.reset();
    g_name.clear();
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
