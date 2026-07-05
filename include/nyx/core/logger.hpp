// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/error.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace nyx {

/// Thread-safe, minimal logger. Default sink is stderr; tests redirect it
/// to a string buffer to make assertions easy.
class Logger {
public:
    static Logger& instance();

    /// Sets the lowest severity that will be emitted. Default: Info.
    void set_level(LogLevel l) noexcept { level_.store(static_cast<int>(l), std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const noexcept { return static_cast<LogLevel>(level_.load(std::memory_order_relaxed)); }

    /// Sets the output sink. Caller owns the stream. Passing nullptr resets
    /// to stderr. Not thread-safe with respect to concurrent log calls
    /// (call only once at startup).
    void set_sink(std::ostream* sink) noexcept;

    void log(LogLevel l, std::string_view msg) const;

    void trace(std::string_view m) const { log(LogLevel::Trace, m); }
    void debug(std::string_view m) const { log(LogLevel::Debug, m); }
    void info (std::string_view m) const { log(LogLevel::Info,  m); }
    void warn (std::string_view m) const { log(LogLevel::Warn,  m); }
    void error(std::string_view m) const { log(LogLevel::Error, m); }
    void crit (std::string_view m) const { log(LogLevel::Critical, m); }

private:
    Logger() = default;
    std::atomic<int>       level_{static_cast<int>(LogLevel::Info)};
    std::ostream*          sink_{nullptr};  // null => stderr
    mutable std::mutex     mu_;
};

namespace detail {
/// Composes `[file:line] msg` for the NYX_LOG* macros.
[[nodiscard]] std::string log_prefix(const char* file, int line, std::string_view msg);
}  // namespace detail

}  // namespace nyx

// Convenience macros - avoid macro hell where possible, but logging benefits
// from __FILE__/__LINE__ capture.
#define NYX_LOG(level, msg) \
    ::nyx::Logger::instance().log(::nyx::LogLevel::level, ::nyx::detail::log_prefix(__FILE__, __LINE__, (msg)))

#define NYX_TRACE(msg) NYX_LOG(Trace,    msg)
#define NYX_DEBUG(msg) NYX_LOG(Debug,    msg)
#define NYX_INFO(msg)  NYX_LOG(Info,     msg)
#define NYX_WARN(msg)  NYX_LOG(Warn,     msg)
#define NYX_ERROR(msg) NYX_LOG(Error,    msg)
#define NYX_CRIT(msg)  NYX_LOG(Critical, msg)
