// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace nyx {

/// Severity levels understood by the Logger. Verbose and Debug are silenced
/// unless the CLI/user opts in.
enum class LogLevel : std::uint8_t {
    Trace    = 0,
    Debug    = 1,
    Info     = 2,
    Warn     = 3,
    Error    = 4,
    Critical = 5,
};

[[nodiscard]] std::string_view to_string(LogLevel l) noexcept;

/// Generic Nyx error type. Carries a category (parser, lifter, ...) so the
/// CLI can produce actionable diagnostics instead of bare string messages.
class Error : public std::runtime_error {
public:
    enum class Category : std::uint8_t {
        Generic,
        Io,
        Parser,
        Disassembler,
        Lifter,
        Decompiler,
        Output,
        InvalidArgument,
    };

    Error(Category c, std::string msg)
        : std::runtime_error(format(c, msg)), category_(c), msg_(std::move(msg)) {}

    [[nodiscard]] Category category() const noexcept { return category_; }
    [[nodiscard]] std::string_view message() const noexcept { return msg_; }

    static std::string format(Category c, std::string_view msg);

private:
    Category    category_;
    std::string msg_;
};

#define NYX_THROW(cat, msg) ::nyx::detail::throw_at(__FILE__, __LINE__, ::nyx::Error::Category::cat, (msg))

namespace detail {
[[noreturn]] void throw_at(const char* file, int line, Error::Category cat, std::string_view msg);
}  // namespace detail

}  // namespace nyx
