// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/core/logger.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace nyx {

Logger& Logger::instance() {
    static Logger l;
    return l;
}

void Logger::set_sink(std::ostream* sink) noexcept {
    sink_ = sink;
}

void Logger::log(LogLevel l, std::string_view msg) const {
    if (static_cast<int>(l) < level_.load(std::memory_order_relaxed)) return;

    std::ostringstream os;
    os << '[' << to_string(l) << "] " << msg << '\n';
    const std::string s = os.str();

    std::lock_guard<std::mutex> lk(mu_);
    if (sink_) {
        sink_->write(s.data(), static_cast<std::streamsize>(s.size()));
        sink_->flush();
    } else {
        std::fwrite(s.data(), 1, s.size(), stderr);
        std::fflush(stderr);
    }
}

namespace detail {

std::string log_prefix(const char* file, int line, std::string_view msg) {
    std::string s;
    s.reserve(32 + msg.size());
    // Trim to basename - cheaper than <filesystem> for hot path.
    const char* base = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    s.append(base);
    s.push_back(':');
    s.append(std::to_string(line));
    s.append(": ");
    s.append(msg);
    return s;
}

}  // namespace detail

}  // namespace nyx
