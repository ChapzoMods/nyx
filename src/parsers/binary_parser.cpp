// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/parsers/binary_parser.hpp"

#include "nyx/core/logger.hpp"
#include "nyx/parsers/dwarf_parser.hpp"
#include "nyx/parsers/elf_parser.hpp"
#include "nyx/parsers/macho_parser.hpp"
#include "nyx/parsers/pe_parser.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

namespace nyx {

std::string_view to_string(BinaryFormat f) noexcept {
    switch (f) {
        case BinaryFormat::Unknown: return "unknown";
        case BinaryFormat::Elf:     return "elf";
        case BinaryFormat::Pe:      return "pe";
        case BinaryFormat::MachO:   return "mach-o";
    }
    return "unknown";
}

std::optional<BinaryFormat> format_from_name(std::string_view s) noexcept {
    if (s == "elf" || s == "ELF")                return BinaryFormat::Elf;
    if (s == "pe"  || s == "PE"  || s == "PE32" || s == "PE32+") return BinaryFormat::Pe;
    if (s == "mach-o" || s == "macho" || s == "Mach-O") return BinaryFormat::MachO;
    return std::nullopt;
}

const Section* BinaryInfo::code_section() const noexcept {
    const Section* best = nullptr;
    for (const auto& s : sections) {
        if (s.is_code && s.file_size > 0) {
            // Prefer the first one - sections are usually in load order.
            return &s;
        }
        if (s.executable && s.file_size > 0 && best == nullptr) best = &s;
    }
    return best;
}

const Section* BinaryInfo::find_section(std::string_view name) const noexcept {
    for (const auto& s : sections) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const Symbol* BinaryInfo::find_symbol(std::string_view name) const noexcept {
    for (const auto& s : symbols) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BinaryParser factory
// ---------------------------------------------------------------------------
std::unique_ptr<BinaryParser> BinaryParser::detect(ByteView magic) {
    if (magic.size() < 4) return nullptr;

    // ELFs start with 0x7f 'E' 'L' 'F'.
    if (magic[0] == 0x7Fu && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        return std::make_unique<ElfParser>();
    }
    // PE: starts with "MZ" (0x4D 0x5A). Real check requires looking at the
    // PE header offset at 0x3C, but accept the MZ signature - false
    // positives on Mach-O/ELF are impossible because of magic overlap.
    if (magic[0] == 'M' && magic[1] == 'Z') {
        return std::make_unique<PeParser>();
    }
    // Mach-O: 0xFEEDFACE / 0xFEEDFACF / 0xCEFAEDFE / 0xCFFAEDFE / fat 0xCAFEBABE / 0xBEBAFECA
    const std::uint32_t magic32 = read_u32_be(magic.data());
    const std::uint32_t magic32le = read_u32_le(magic.data());
    constexpr std::uint32_t MH_MAGIC      = 0xFEEDFACEu;
    constexpr std::uint32_t MH_MAGIC_64   = 0xFEEDFACFu;
    constexpr std::uint32_t FAT_MAGIC     = 0xCAFEBABEu;
    if (magic32 == MH_MAGIC || magic32 == MH_MAGIC_64
        || magic32le == MH_MAGIC || magic32le == MH_MAGIC_64
        || magic32 == FAT_MAGIC || magic32le == FAT_MAGIC) {
        return std::make_unique<MachOParser>();
    }
    return nullptr;
}

BinaryInfo BinaryParser::load_and_parse(const std::string& path) {
    auto buf = ByteBuffer::from_file(path);
    if (!buf) {
        NYX_THROW(Io, "failed to load binary file");
    }
    auto parser = detect(buf->view());
    if (!parser) {
        NYX_THROW(Parser, "unrecognised file format - not ELF, PE, or Mach-O");
    }
    BinaryInfo info = parser->parse(buf->view());
    info.path = path;
    // v0.0.6: auto-load DWARF if debug sections are present.
    load_dwarf(info, path);
    return info;
}

void BinaryParser::load_dwarf(BinaryInfo& bin, const std::string& path) {
    // Check if the binary has any .debug_* sections.
    bool has_debug = false;
    for (const auto& s : bin.sections) {
        if (s.name.rfind(".debug_", 0) == 0) { has_debug = true; break; }
    }
    if (!has_debug) return;

    auto buf = ByteBuffer::from_file(path);
    if (!buf) {
        NYX_WARN("load_dwarf: cannot re-open '" + path + "'");
        return;
    }
    try {
        auto dwarf = parse_dwarf(bin, buf->view());
        if (dwarf.has_info) {
            bin.dwarf = std::make_shared<DwarfInfo>(std::move(dwarf));
            NYX_INFO("DWARF: loaded debug info for " + path);
        }
    } catch (const std::exception& e) {
        NYX_WARN(std::string("DWARF: load failed: ") + e.what());
    }
}

}  // namespace nyx
