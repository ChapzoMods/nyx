// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx {

/// A single line-table entry mapping a PC to a source location.
struct DwarfLineEntry {
    std::uint64_t address  = 0;
    std::uint32_t file     = 0;   // index into the file_names table
    std::uint32_t line     = 0;
    std::uint32_t column   = 0;
    bool         is_stmt   = false;
    bool         end_seq   = false;
};

/// A function described in DWARF (from .debug_info DIEs).
struct DwarfFunction {
    std::string  name;
    std::uint64_t low_pc  = 0;
    std::uint64_t high_pc = 0;   // absolute address (resolved if high_pc is const)
    std::uint64_t type_offset = 0;  // offset to the type DIE, 0 if none
    bool         has_range = false;
};

/// A DWARF type DIE resolved to a primitive C type name.
struct DwarfType {
    std::uint64_t offset = 0;
    std::string   name;          // "int", "char", "long", "void*", etc.
    enum class Kind : std::uint8_t {
        Unknown, Base, Pointer, Typedef, Struct, Union, Enum, Function
    } kind = Kind::Unknown;
    std::uint8_t byte_size = 0;  // for base types
    /// For pointers: offset of the pointed-to type.
    std::uint64_t pointee_offset = 0;
};

/// Result of parsing DWARF sections from a binary.
struct DwarfInfo {
    /// Line table: PC -> source location. Sorted by address.
    std::vector<DwarfLineEntry> lines;

    /// File names from the line-table header (1-indexed in DWARF).
    std::vector<std::string> file_names;

    /// Functions described in .debug_info.
    std::vector<DwarfFunction> functions;

    /// Types resolved from .debug_info, keyed by DIE offset.
    std::unordered_map<std::uint64_t, DwarfType> types;

    /// True if any DWARF section was successfully parsed.
    bool has_info = false;

    /// Looks up the source location for a given address. Returns nullopt
    /// if no line entry covers the address.
    [[nodiscard]] std::optional<DwarfLineEntry> lookup_address(std::uint64_t addr) const noexcept;

    /// Looks up the function name for a given address.
    [[nodiscard]] std::optional<std::string> function_name_at(std::uint64_t addr) const noexcept;

    /// Returns the file name for a 1-indexed file number, or empty.
    [[nodiscard]] std::string file_name(std::uint32_t idx) const noexcept;

    /// Resolves a type chain starting at `offset` to a C type string.
    /// Follows typedefs and pointers to produce a final type name.
    [[nodiscard]] std::string resolve_type_name(std::uint64_t offset) const noexcept;
};

/// Parses DWARF v4 sections from `data` (the raw file bytes). `bin`
/// provides the section table so the parser can locate .debug_line,
/// .debug_info, .debug_abbrev and .debug_str.
///
/// The parser is intentionally conservative: any corrupt section is
/// skipped (logged via NYX_WARN) and the rest are still parsed. It
/// handles DWARF32 and DWARF64 formats, and respects the binary's
/// endianness.
[[nodiscard]] DwarfInfo parse_dwarf(const BinaryInfo& bin, ByteView data);

}  // namespace nyx
