// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx {

// Forward declaration to avoid circular include.
struct DwarfInfo;

/// File format detected by the magic-bytes probe in BinaryParser::detect.
enum class BinaryFormat : std::uint8_t {
    Unknown = 0,
    Elf,
    Pe,
    MachO,
    Wasm,
};

[[nodiscard]] std::string_view to_string(BinaryFormat f) noexcept;
[[nodiscard]] std::optional<BinaryFormat> format_from_name(std::string_view s) noexcept;

/// Generic section/segment descriptor produced by every parser.
struct Section {
    std::string  name;            // ".text", ".data", ".rdata", ".__TEXT" ...
    std::uint64_t vaddr      = 0; // virtual address (file layout for PE)
    std::uint64_t file_off   = 0; // offset inside the file
    std::uint64_t file_size  = 0; // raw size on disk
    std::uint64_t mem_size   = 0; // virtual size (can differ from file_size)
    std::uint32_t flags      = 0; // parser-specific bitfield
    bool         executable  = false;
    bool         writable    = false;
    bool         readable    = true;
    bool         is_code     = false;     // parser hints (e.g. SHF_EXECINSTR)
    /// ELF-only: section has no file backing (SHT_NOBITS, e.g. .bss/.tbss).
    /// When true, `file_size` is 0 and `mem_size` reflects the runtime size;
    /// readers must NOT try to dereference bytes at `file_off`.
    bool         is_nobits   = false;
};

/// Generic symbol descriptor.
struct Symbol {
    std::string   name;
    std::uint64_t value = 0;        // address or offset
    std::uint64_t size  = 0;
    enum class Kind : std::uint8_t { Unknown, Function, Object, Section, File } kind = Kind::Unknown;
    bool          imported = false;
    bool          exported = false;
    /// PE-only: when `imported` is true and the symbol was resolved by
    /// ordinal rather than by name, this holds the ordinal value (> 0).
    /// Zero means "not an ordinal import" or "not a PE symbol".
    std::uint16_t ordinal = 0;
    /// For PE imports: the DLL name without path. Empty for non-PE symbols.
    std::string   module;
};

/// Result of parsing: a uniform view over the binary, format-agnostic.
struct BinaryInfo {
    std::string       path;
    BinaryFormat      format       = BinaryFormat::Unknown;
    Arch              arch         = Arch::Unknown;
    Endian            endian       = Endian::Little;
    bool              is_64bit     = false;
    bool              is_pie       = false;
    bool              has_nx       = false;       // NX bit / DEP
    bool              has_relro    = false;
    bool              has_canary   = false;
    bool              stripped     = false;

    std::uint64_t     entry_point = 0;
    std::uint64_t     image_base  = 0;           // PE only; 0 for ELF/Mach-O

    std::vector<Section> sections;
    std::vector<Symbol>  symbols;

    /// Mach-O fat/universal archives only: every contained slice, fully
    /// parsed. `arch`/`sections`/`symbols` above always describe the
    /// "primary" slice (the first one Nyx recognised); the remaining
    /// slices live here so callers can iterate them. Empty for ELF/PE.
    std::vector<BinaryInfo> slices;

    /// v0.0.6: DWARF debug info parsed from .debug_line / .debug_info /
    /// .debug_abbrev / .debug_str. Populated lazily by
    /// BinaryParser::load_dwarf() when debug sections are present.
    /// Nullptr when the binary has no DWARF or it hasn't been loaded yet.
    std::shared_ptr<DwarfInfo> dwarf;

    /// v0.3.0: strings extracted from .rodata/.rdata, mapped by their virtual address.
    std::unordered_map<std::uint64_t, std::string> rodata_strings;

    /// Returns the first executable section (typically .text or __TEXT).
    [[nodiscard]] const Section* code_section() const noexcept;

    /// Looks up a section by name (case-sensitive).
    [[nodiscard]] const Section* find_section(std::string_view name) const noexcept;

    /// Looks up a symbol by exact name.
    [[nodiscard]] const Symbol* find_symbol(std::string_view name) const noexcept;
};

}  // namespace nyx
