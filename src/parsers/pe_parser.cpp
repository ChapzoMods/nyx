// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/parsers/pe_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace nyx {

namespace {

// DOS header: e_magic at 0, e_lfanew at offset 0x3C (uint32).
constexpr std::uint16_t IMAGE_DOS_SIGNATURE = 0x5A4Du;  // "MZ"
constexpr std::uint32_t IMAGE_NT_SIGNATURE   = 0x00004550u;  // "PE\0\0"

// Machine values
constexpr std::uint16_t IMAGE_FILE_MACHINE_I386     = 0x014cu;
constexpr std::uint16_t IMAGE_FILE_MACHINE_AMD64    = 0x8664u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_ARM      = 0x01c0u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_ARMNT    = 0x01c4u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_ARM64    = 0xAA64u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_POWERPC  = 0x01F0u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_POWERPCFP= 0x01F1u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_MIPS     = 0x0166u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_R3000    = 0x0162u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_R4000    = 0x0166u;
constexpr std::uint16_t IMAGE_FILE_MACHINE_WCEMIPSV2= 0x0169u;

// Section characteristics
constexpr std::uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000u;
constexpr std::uint32_t IMAGE_SCN_MEM_READ    = 0x40000000u;
constexpr std::uint32_t IMAGE_SCN_MEM_WRITE   = 0x80000000u;
constexpr std::uint32_t IMAGE_SCN_CNT_CODE    = 0x00000020u;
constexpr std::uint32_t IMAGE_SCN_CNT_INIT_DATA = 0x00000040u;
constexpr std::uint32_t IMAGE_SCN_CNT_UNINIT_DATA = 0x00000080u;

// DLL characteristics
constexpr std::uint16_t IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020u;
constexpr std::uint16_t IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE    = 0x0040u;  // PIE/ASLR
constexpr std::uint16_t IMAGE_DLLCHARACTERISTICS_NX_COMPAT       = 0x0100u;  // DEP/NX
constexpr std::uint16_t IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY = 0x0080u;
constexpr std::uint16_t IMAGE_DLLCHARACTERISTICS_GUARD_CF        = 0x4000u;

// Section header layout (40 bytes)
struct SectionHeader {
    char          name[8];
    std::uint32_t virtual_size;
    std::uint32_t virtual_address;
    std::uint32_t size_of_raw_data;
    std::uint32_t pointer_to_raw_data;
    std::uint32_t pointer_to_relocations;
    std::uint32_t pointer_to_linenumbers;
    std::uint16_t number_of_relocations;
    std::uint16_t number_of_linenumbers;
    std::uint32_t characteristics;
};
static_assert(sizeof(SectionHeader) == 40);

}  // namespace

bool PeParser::accepts(ByteView magic) const noexcept {
    return magic.size() >= 2
        && magic[0] == 'M' && magic[1] == 'Z';
}

BinaryInfo PeParser::parse(ByteView data) const {
    if (data.size() < 0x40) NYX_THROW(Parser, "PE: file too small for DOS header");
    if (data[0] != 'M' || data[1] != 'Z') NYX_THROW(Parser, "PE: missing MZ signature");

    const std::uint32_t e_lfanew = read_u32_le(data.data() + 0x3C);
    if (e_lfanew + 4 > data.size()) NYX_THROW(Parser, "PE: e_lfanew out of bounds");
    if (read_u32_le(data.data() + e_lfanew) != IMAGE_NT_SIGNATURE) {
        NYX_THROW(Parser, "PE: missing 'PE\\0\\0' signature");
    }

    const std::size_t coff_off = e_lfanew + 4;
    if (coff_off + 20 > data.size()) NYX_THROW(Parser, "PE: truncated COFF header");

    const std::uint16_t machine    = read_u16_le(data.data() + coff_off + 0);
    const std::uint16_t n_sections = read_u16_le(data.data() + coff_off + 2);
    const std::uint16_t opt_size   = read_u16_le(data.data() + coff_off + 16);
    const std::uint16_t characteristics = read_u16_le(data.data() + coff_off + 18);

    BinaryInfo info{};
    info.format   = BinaryFormat::Pe;
    info.endian   = Endian::Little;

    switch (machine) {
        case IMAGE_FILE_MACHINE_I386:  info.arch = Arch::X86; info.is_64bit = false; break;
        case IMAGE_FILE_MACHINE_AMD64: info.arch = Arch::X86_64; info.is_64bit = true; break;
        case IMAGE_FILE_MACHINE_ARM:
        case IMAGE_FILE_MACHINE_ARMNT: info.arch = Arch::ARM; info.is_64bit = false; break;
        case IMAGE_FILE_MACHINE_ARM64: info.arch = Arch::AARCH64; info.is_64bit = true; break;
        case IMAGE_FILE_MACHINE_POWERPC:
        case IMAGE_FILE_MACHINE_POWERPCFP: info.arch = Arch::PPC; info.is_64bit = false; break;
        case IMAGE_FILE_MACHINE_R3000:
        case IMAGE_FILE_MACHINE_R4000:
        case IMAGE_FILE_MACHINE_WCEMIPSV2: info.arch = Arch::MIPS; info.is_64bit = false; break;
        default:
            info.arch = Arch::Unknown;
            NYX_WARN("PE: unhandled machine=0x" + to_hex(machine, 4, false));
            break;
    }
    info.is_pie = (characteristics & 0x2000u) != 0;  // IMAGE_FILE_DLL is 0x2000; PIE is approximate via DLL flag
    // A PE DLL has its base relocated at load time, similar in spirit to PIE.

    // Optional header
    const std::size_t opt_off = coff_off + 20;
    if (opt_off + opt_size > data.size()) NYX_THROW(Parser, "PE: truncated optional header");

    const std::uint16_t opt_magic = read_u16_le(data.data() + opt_off);
    const bool is_pe32_plus = (opt_magic == 0x20B);   // PE32+ = 0x20B, PE32 = 0x10B
    if (opt_magic != 0x10B && opt_magic != 0x20B) {
        NYX_THROW(Parser, "PE: invalid optional header magic");
    }

    std::uint64_t image_base = 0;
    std::uint32_t entry_rva  = 0;
    std::uint16_t dll_chars  = 0;

    if (is_pe32_plus) {
        entry_rva  = read_u32_le(data.data() + opt_off + 16);
        image_base = read_u64_le(data.data() + opt_off + 24);
        // BaseRelocTable, Debug dir, ... - DLL characteristics at opt_off + 70
        if (opt_off + 72 <= data.size())
            dll_chars = read_u16_le(data.data() + opt_off + 70);
    } else {
        entry_rva  = read_u32_le(data.data() + opt_off + 16);
        image_base = read_u32_le(data.data() + opt_off + 28);
        if (opt_off + 70 <= data.size())
            dll_chars = read_u16_le(data.data() + opt_off + 70);
    }
    info.image_base    = image_base;
    info.entry_point   = image_base + entry_rva;
    info.has_nx        = (dll_chars & IMAGE_DLLCHARACTERISTICS_NX_COMPAT)    != 0;
    info.has_relro     = false;  // not a PE concept; left false
    info.has_canary    = false;  // would need to scan imports for __security_cookie
    info.stripped      = (characteristics & 0x0008u) == 0u;  // IMAGE_FILE_DEBUG_STRIPPED? Actually 0x0008 is IMAGE_FILE_LINE_NUMS_STRIPPED. We approximate "stripped" with no symbol table on disk (COFF symbols are rare in PE images).

    // Section table
    const std::size_t sect_off = opt_off + opt_size;
    if (sect_off + static_cast<std::size_t>(n_sections) * sizeof(SectionHeader) > data.size()) {
        NYX_THROW(Parser, "PE: section table out of bounds");
    }
    for (std::uint16_t i = 0; i < n_sections; ++i) {
        SectionHeader h{};
        std::memcpy(&h, data.data() + sect_off + i * sizeof(SectionHeader), sizeof(SectionHeader));

        Section s{};
        // Section names are 8-byte ASCII, NOT null-terminated if exactly 8 chars.
        const std::size_t n = strnlen(h.name, 8);
        s.name.assign(h.name, n);
        s.vaddr     = h.virtual_address;
        s.file_off  = h.pointer_to_raw_data;
        s.file_size = h.size_of_raw_data;
        s.mem_size  = h.virtual_size;
        s.flags     = h.characteristics;
        s.executable = (h.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        s.readable   = (h.characteristics & IMAGE_SCN_MEM_READ)    != 0;
        s.writable   = (h.characteristics & IMAGE_SCN_MEM_WRITE)   != 0;
        s.is_code    = (h.characteristics & IMAGE_SCN_CNT_CODE)    != 0;
        info.sections.push_back(s);
    }

    // Import / Export directory walk - very lightweight. The export dir
    // gives us exported symbol names, the import dir gives us imported
    // DLL!Symbol names. We don't fully parse IATs.
    if (is_pe32_plus) {
        if (opt_off + 112 + 8 <= data.size()) {
            const std::uint32_t export_rva  = read_u32_le(data.data() + opt_off + 112);
            const std::uint32_t export_size = read_u32_le(data.data() + opt_off + 116);
            const std::uint32_t import_rva  = read_u32_le(data.data() + opt_off + 120);
            const std::uint32_t import_size = read_u32_le(data.data() + opt_off + 124);
            walk_exports(info, data, export_rva, export_size, image_base);
            walk_imports(info, data, import_rva, import_size, image_base);
        }
    } else {
        if (opt_off + 96 + 8 <= data.size()) {
            const std::uint32_t export_rva  = read_u32_le(data.data() + opt_off + 96);
            const std::uint32_t export_size = read_u32_le(data.data() + opt_off + 100);
            const std::uint32_t import_rva  = read_u32_le(data.data() + opt_off + 104);
            const std::uint32_t import_size = read_u32_le(data.data() + opt_off + 108);
            walk_exports(info, data, export_rva, export_size, image_base);
            walk_imports(info, data, import_rva, import_size, image_base);
        }
    }

    // Add a synthetic symbol for the entry point so the decompiler can pick it up.
    {
        Symbol ep{};
        ep.name     = "entry";
        ep.value    = info.entry_point;
        ep.kind     = Symbol::Kind::Function;
        ep.exported = true;
        info.symbols.push_back(ep);
    }

    return info;
}

// ---------------------------------------------------------------------------
// RVA -> file offset resolution lives in the PeParser member function
// rva_to_file_offset_fallback; the anonymous-namespace helper was removed
// to keep the translation unit lean.
// ---------------------------------------------------------------------------

void PeParser::walk_exports(BinaryInfo& info, ByteView data,
                            std::uint32_t rva, std::uint32_t size,
                            std::uint64_t image_base) const {
    if (rva == 0 || size == 0) return;
    // Resolve via section table - we need n_sections + opt_off again.
    // Re-parse the small header pieces instead of carrying them around.
    if (data.size() < 0x40) return;
    const std::uint32_t e_lfanew = read_u32_le(data.data() + 0x3C);
    const std::size_t coff_off = e_lfanew + 4;
    const std::uint16_t n_sections = read_u16_le(data.data() + coff_off + 2);
    const std::uint16_t opt_size   = read_u16_le(data.data() + coff_off + 16);
    const std::size_t opt_off = coff_off + 20;
    const std::size_t sect_off = opt_off + opt_size;

    const std::uint32_t file_off = rva_to_file_offset_fallback(data, sect_off, n_sections, rva, image_base);
    if (file_off == 0 || file_off + 40 > data.size()) return;

    // IMAGE_EXPORT_DIRECTORY (40 bytes)
    const std::uint32_t name_rva        = read_u32_le(data.data() + file_off + 12);
    const std::uint32_t base_ordinal    = read_u32_le(data.data() + file_off + 16);
    const std::uint32_t n_funcs         = read_u32_le(data.data() + file_off + 20);
    const std::uint32_t n_names         = read_u32_le(data.data() + file_off + 24);
    const std::uint32_t funcs_rva       = read_u32_le(data.data() + file_off + 28);
    const std::uint32_t names_rva       = read_u32_le(data.data() + file_off + 32);
    const std::uint32_t ordinals_rva    = read_u32_le(data.data() + file_off + 36);
    (void)base_ordinal;

    const std::uint32_t funcs_off    = rva_to_file_offset_fallback(data, sect_off, n_sections, funcs_rva, image_base);
    const std::uint32_t names_off    = rva_to_file_offset_fallback(data, sect_off, n_sections, names_rva, image_base);
    const std::uint32_t ordinals_off = rva_to_file_offset_fallback(data, sect_off, n_sections, ordinals_rva, image_base);
    (void)name_rva;

    if (n_names == 0 || funcs_off == 0 || names_off == 0 || ordinals_off == 0) return;
    if (funcs_off + 4u * n_funcs > data.size()) return;
    if (names_off + 4u * n_names > data.size()) return;
    if (ordinals_off + 2u * n_names > data.size()) return;

    for (std::uint32_t i = 0; i < n_names; ++i) {
        const std::uint32_t name_rva_i = read_u32_le(data.data() + names_off + i * 4);
        const std::uint16_t ordinal    = read_u16_le(data.data() + ordinals_off + i * 2);
        if (ordinal >= n_funcs) continue;
        const std::uint32_t func_rva   = read_u32_le(data.data() + funcs_off + ordinal * 4);
        const std::uint32_t name_off   = rva_to_file_offset_fallback(data, sect_off, n_sections, name_rva_i, image_base);
        if (name_off == 0 || name_off >= data.size()) continue;

        const char* base = reinterpret_cast<const char*>(data.data());
        std::string nm;
        for (std::size_t j = name_off; j < data.size() && base[j] != '\0'; ++j) nm.push_back(base[j]);
        if (nm.empty()) continue;

        Symbol sym{};
        sym.name     = nm;
        sym.value    = image_base + func_rva;
        sym.kind     = Symbol::Kind::Function;
        sym.exported = true;
        info.symbols.push_back(sym);
    }
}

void PeParser::walk_imports(BinaryInfo& info, ByteView data,
                            std::uint32_t rva, std::uint32_t size,
                            std::uint64_t image_base) const {
    if (rva == 0 || size == 0) return;
    if (data.size() < 0x40) return;
    const std::uint32_t e_lfanew = read_u32_le(data.data() + 0x3C);
    const std::size_t coff_off = e_lfanew + 4;
    const std::uint16_t n_sections = read_u16_le(data.data() + coff_off + 2);
    const std::uint16_t opt_size   = read_u16_le(data.data() + coff_off + 16);
    const std::size_t opt_off = coff_off + 20;
    const std::size_t sect_off = opt_off + opt_size;

    std::uint32_t off = rva_to_file_offset_fallback(data, sect_off, n_sections, rva, image_base);
    if (off == 0) return;

    // Each import descriptor is 20 bytes; the table ends with a zero entry.
    while (off + 20 <= data.size()) {
        const std::uint32_t original_first_thunk = read_u32_le(data.data() + off + 0);
        const std::uint32_t name_rva             = read_u32_le(data.data() + off + 12);
        const std::uint32_t first_thunk          = read_u32_le(data.data() + off + 16);
        if (original_first_thunk == 0 && name_rva == 0 && first_thunk == 0) break;

        // DLL name
        const std::uint32_t name_off = rva_to_file_offset_fallback(data, sect_off, n_sections, name_rva, image_base);
        std::string dll_name;
        if (name_off && name_off < data.size()) {
            const char* base = reinterpret_cast<const char*>(data.data());
            for (std::size_t j = name_off; j < data.size() && base[j] != '\0'; ++j) dll_name.push_back(base[j]);
        }

        const std::uint32_t thunk_rva = original_first_thunk ? original_first_thunk : first_thunk;
        std::uint32_t thunk_off = rva_to_file_offset_fallback(data, sect_off, n_sections, thunk_rva, image_base);
        if (thunk_off == 0) { off += 20; continue; }

        // Walk the thunk array; size depends on PE32 vs PE32+.
        const bool is_pe32_plus = info.is_64bit;
        const std::size_t entry_size = is_pe32_plus ? 8 : 4;
        const std::uint64_t import_flag = is_pe32_plus ? 0x8000000000000000ull : 0x80000000ull;
        std::size_t i = 0;
        while (thunk_off + i * entry_size + entry_size <= data.size()) {
            const std::uint64_t v = is_pe32_plus
                ? read_u64_le(data.data() + thunk_off + i * entry_size)
                : read_u32_le(data.data() + thunk_off + i * entry_size);
            if (v == 0) break;
            ++i;

            // Ordinal import - skip name lookup.
            if ((v & import_flag) != 0) {
                Symbol sym{};
                sym.name = dll_name + "!ord" + std::to_string(v & 0xFFFFu);
                sym.kind = Symbol::Kind::Function;
                sym.imported = true;
                info.symbols.push_back(sym);
                continue;
            }

            // Hint/Name table: 2-byte hint + null-terminated name.
            const std::uint32_t hn_rva = static_cast<std::uint32_t>(v & 0x7FFFFFFFu);
            const std::uint32_t hn_off = rva_to_file_offset_fallback(data, sect_off, n_sections, hn_rva, image_base);
            if (hn_off == 0 || hn_off + 2 >= data.size()) continue;
            const char* base = reinterpret_cast<const char*>(data.data());
            std::string nm;
            for (std::size_t j = hn_off + 2; j < data.size() && base[j] != '\0'; ++j) nm.push_back(base[j]);
            if (nm.empty()) continue;
            Symbol sym{};
            sym.name = dll_name.empty() ? nm : (dll_name + "!" + nm);
            sym.kind = Symbol::Kind::Function;
            sym.imported = true;
            info.symbols.push_back(sym);
        }

        off += 20;
    }
}

std::uint32_t PeParser::rva_to_file_offset_fallback(ByteView data,
                                                     std::size_t sect_off,
                                                     std::uint16_t n_sections,
                                                     std::uint32_t rva,
                                                     std::uint64_t image_base) const {
    // PE sections use RVA relative to image_base; subtract it if the caller
    // passed a VA instead (defensive).
    if (image_base && rva >= image_base) rva = static_cast<std::uint32_t>(rva - image_base);

    for (std::uint16_t i = 0; i < n_sections; ++i) {
        SectionHeader h{};
        if (sect_off + (i + 1) * sizeof(SectionHeader) > data.size()) break;
        std::memcpy(&h, data.data() + sect_off + i * sizeof(SectionHeader), sizeof(SectionHeader));
        const std::uint32_t vsize = h.virtual_size ? h.virtual_size : h.size_of_raw_data;
        if (rva >= h.virtual_address && rva < h.virtual_address + vsize) {
            return h.pointer_to_raw_data + (rva - h.virtual_address);
        }
    }
    return 0;
}

}  // namespace nyx
