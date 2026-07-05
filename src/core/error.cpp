// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/core/error.hpp"

#include <sstream>
#include <string>

namespace nyx {

std::string_view to_string(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace:    return "trace";
        case LogLevel::Debug:    return "debug";
        case LogLevel::Info:     return "info";
        case LogLevel::Warn:     return "warn";
        case LogLevel::Error:    return "error";
        case LogLevel::Critical: return "critical";
    }
    return "unknown";
}

std::string Error::format(Category c, std::string_view msg) {
    std::ostringstream os;
    os << '[';
    switch (c) {
        case Category::Generic:        os << "generic";     break;
        case Category::Io:             os << "io";          break;
        case Category::Parser:         os << "parser";      break;
        case Category::Disassembler:   os << "disasm";      break;
        case Category::Lifter:         os << "lifter";      break;
        case Category::Decompiler:     os << "decompiler";  break;
        case Category::Output:         os << "output";      break;
        case Category::InvalidArgument:os << "arg";         break;
    }
    os << "] " << msg;
    return os.str();
}

namespace detail {

[[noreturn]] void throw_at(const char* file, int line, Error::Category cat, std::string_view msg) {
    std::string full;
    full.reserve(std::string_view(file).size() + msg.size() + 16);
    full += file;
    full += ':';
    full += std::to_string(line);
    full += ": ";
    full += msg;
    throw Error(cat, full);
}

}  // namespace detail

}  // namespace nyx
