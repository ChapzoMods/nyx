// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nyx {

/// Abstract base for every binary-format parser (ELF / PE / Mach-O).
/// Concrete parsers are expected to be cheap to construct; all heavy work
/// happens in `parse()`.
class BinaryParser {
public:
    virtual ~BinaryParser() = default;

    BinaryParser() = default;
    BinaryParser(const BinaryParser&)            = delete;
    BinaryParser& operator=(const BinaryParser&) = delete;
    BinaryParser(BinaryParser&&) noexcept        = default;
    BinaryParser& operator=(BinaryParser&&) noexcept = default;

    /// Returns the format this parser handles.
    [[nodiscard]] virtual BinaryFormat format() const noexcept = 0;

    /// Probes the first `n` bytes and reports whether this parser accepts
    /// them. Must not throw.
    [[nodiscard]] virtual bool accepts(ByteView magic) const noexcept = 0;

    /// Parses the entire buffer and returns a populated BinaryInfo.
    /// Throws `Error` on malformed input.
    [[nodiscard]] virtual BinaryInfo parse(ByteView data) const = 0;

    /// Factory: returns the right parser for the buffer's magic bytes, or
    /// nullptr if the format is unknown.
    [[nodiscard]] static std::unique_ptr<BinaryParser> detect(ByteView magic);

    /// Convenience: loads a file from disk, detects the format and parses it.
    [[nodiscard]] static BinaryInfo load_and_parse(const std::string& path);

    /// v0.0.6: loads DWARF debug info from `path` into `bin.dwarf`.
    /// No-op if the binary has no .debug_* sections. Safe to call on
    /// any format; only ELF and Mach-O currently carry DWARF.
    static void load_dwarf(BinaryInfo& bin, const std::string& path);
};

}  // namespace nyx
