// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/parsers/elf_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace nyx {

namespace {

// ELF magic
constexpr std::uint8_t ELFMAG0 = 0x7fu;
constexpr std::uint8_t ELFMAG1 = 'E';
constexpr std::uint8_t ELFMAG2 = 'L';
constexpr std::uint8_t ELFMAG3 = 'F';

// EI_CLASS values
constexpr std::uint8_t ELFCLASS32 = 1;
constexpr std::uint8_t ELFCLASS64 = 2;

// EI_DATA values
constexpr std::uint8_t ELFDATA2LSB = 1;
constexpr std::uint8_t ELFDATA2MSB = 2;

// e_type values we care about
constexpr std::uint16_t ET_EXEC = 2;
constexpr std::uint16_t ET_DYN  = 3;

// sh_type values
constexpr std::uint32_t SHT_PROGBITS = 1;
constexpr std::uint32_t SHT_SYMTAB   = 2;
constexpr std::uint32_t SHT_STRTAB   = 3;
constexpr std::uint32_t SHT_DYNSYM   = 11;

// sh_flags values
constexpr std::uint64_t SHF_WRITE     = 0x1;
constexpr std::uint64_t SHF_ALLOC     = 0x2;
constexpr std::uint64_t SHF_EXECINSTR = 0x4;

// st_info helpers
inline std::uint8_t st_type(std::uint8_t info)  { return info & 0xF; }
inline std::uint8_t st_bind(std::uint8_t info)  { return info >> 4; }
constexpr std::uint8_t STT_FUNC    = 2;
constexpr std::uint8_t STT_OBJECT  = 1;
constexpr std::uint8_t STT_SECTION = 3;
constexpr std::uint8_t STT_FILE    = 4;
constexpr std::uint8_t STT_NOTYPE  = 0;

// e_machine values
constexpr std::uint16_t EM_386     = 3;
constexpr std::uint16_t EM_ARM     = 40;
constexpr std::uint16_t EM_X86_64  = 62;
constexpr std::uint16_t EM_AARCH64 = 183;
constexpr std::uint16_t EM_PPC     = 20;
constexpr std::uint16_t EM_PPC64   = 21;
constexpr std::uint16_t EM_MIPS    = 8;
constexpr std::uint16_t EM_MIPS_RS3_LE = 10;
constexpr std::uint16_t EM_MIPS_RS4_BE = 0x9026;  // uncommon, kept for completeness

// PT_GNU_STACK marker for NX check
constexpr std::uint32_t PT_GNU_STACK = 0x6474e551u;
constexpr std::uint32_t PT_GNU_RELRO = 0x6474e552u;

struct ElfHeader32 {
    std::uint8_t  ident[16];
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint32_t entry;
    std::uint32_t phoff;
    std::uint32_t shoff;
    std::uint32_t flags;
    std::uint16_t ehsize;
    std::uint16_t phentsize;
    std::uint16_t phnum;
    std::uint16_t shentsize;
    std::uint16_t shnum;
    std::uint16_t shstrndx;
};

struct ElfHeader64 {
    std::uint8_t  ident[16];
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t phoff;
    std::uint64_t shoff;
    std::uint32_t flags;
    std::uint16_t ehsize;
    std::uint16_t phentsize;
    std::uint16_t phnum;
    std::uint16_t shentsize;
    std::uint16_t shnum;
    std::uint16_t shstrndx;
};

struct Shdr32 {
    std::uint32_t name;
    std::uint32_t type;
    std::uint32_t flags;
    std::uint32_t addr;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t link;
    std::uint32_t info;
    std::uint32_t addralign;
    std::uint32_t entsize;
};

struct Shdr64 {
    std::uint32_t name;
    std::uint32_t type;
    std::uint64_t flags;
    std::uint64_t addr;
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t link;
    std::uint32_t info;
    std::uint64_t addralign;
    std::uint64_t entsize;
};

struct Sym32 {
    std::uint32_t name;
    std::uint32_t value;
    std::uint32_t size;
    std::uint8_t  info;
    std::uint8_t  other;
    std::uint16_t shndx;
};

struct Sym64 {
    std::uint32_t name;
    std::uint8_t  info;
    std::uint8_t  other;
    std::uint16_t shndx;
    std::uint64_t value;
    std::uint64_t size;
};

template <typename T>
T read_struct(ByteView data, std::size_t off) {
    if (off + sizeof(T) > data.size()) {
        NYX_THROW(Parser, "ELF: section header out of bounds");
    }
    T t{};
    std::memcpy(&t, data.data() + off, sizeof(T));
    return t;
}

}  // namespace

bool ElfParser::accepts(ByteView magic) const noexcept {
    return magic.size() >= 4
        && magic[0] == ELFMAG0
        && magic[1] == ELFMAG1
        && magic[2] == ELFMAG2
        && magic[3] == ELFMAG3;
}

BinaryInfo ElfParser::parse(ByteView data) const {
    if (!accepts(data)) {
        NYX_THROW(Parser, "ELF: bad magic");
    }
    if (data.size() < 16) {
        NYX_THROW(Parser, "ELF: truncated ident");
    }

    const std::uint8_t ei_class = data[4];
    const std::uint8_t ei_data  = data[5];

    const bool is_64 = (ei_class == ELFCLASS64);
    if (ei_class != ELFCLASS32 && ei_class != ELFCLASS64) {
        NYX_THROW(Parser, "ELF: invalid EI_CLASS");
    }
    const Endian endian = (ei_data == ELFDATA2LSB) ? Endian::Little : Endian::Big;
    const bool le = (endian == Endian::Little);

    auto read_u16 = [&](std::size_t o) { return le ? read_u16_le(data.data() + o) : read_u16_be(data.data() + o); };
    auto read_u32 = [&](std::size_t o) { return le ? read_u32_le(data.data() + o) : read_u32_be(data.data() + o); };
    auto read_u64 = [&](std::size_t o) { return le ? read_u64_le(data.data() + o) : read_u64_be(data.data() + o); };

    BinaryInfo info{};
    info.format   = BinaryFormat::Elf;
    info.endian   = endian;
    info.is_64bit = is_64;

    const std::uint16_t e_type     = read_u16(16);
    const std::uint16_t e_machine  = read_u16(18);

    info.is_pie = (e_type == ET_DYN);

    // Map machine -> Arch
    switch (e_machine) {
        case EM_386:     info.arch = Arch::X86;      break;
        case EM_X86_64:  info.arch = Arch::X86_64;   break;
        case EM_ARM:     info.arch = Arch::ARM;      break;
        case EM_AARCH64: info.arch = Arch::AARCH64;  break;
        case EM_PPC:     info.arch = Arch::PPC;      break;
        case EM_PPC64:   info.arch = Arch::PPC64;    break;
        case EM_MIPS:
        case EM_MIPS_RS3_LE:
            info.arch = is_64 ? Arch::MIPS64 : Arch::MIPS;
            break;
        default:
            info.arch = Arch::Unknown;
            NYX_WARN("ELF: unhandled e_machine=" + std::to_string(e_machine));
            break;
    }

    // Header offsets
    std::uint64_t entry, phoff, shoff;
    std::uint16_t phentsize, phnum, shentsize, shnum, shstrndx;
    if (is_64) {
        if (data.size() < sizeof(ElfHeader64)) NYX_THROW(Parser, "ELF: truncated ehdr64");
        entry     = read_u64(24);
        phoff     = read_u64(32);
        shoff     = read_u64(40);
        phentsize = read_u16(54);
        phnum     = read_u16(56);
        shentsize = read_u16(58);
        shnum     = read_u16(60);
        shstrndx  = read_u16(62);
    } else {
        if (data.size() < sizeof(ElfHeader32)) NYX_THROW(Parser, "ELF: truncated ehdr32");
        entry     = read_u32(24);
        phoff     = read_u32(28);
        shoff     = read_u32(32);
        phentsize = read_u16(42);
        phnum     = read_u16(44);
        shentsize = read_u16(46);
        shnum     = read_u16(48);
        shstrndx  = read_u16(50);
    }
    info.entry_point = entry;

    // Section headers + section name string table
    std::vector<Shdr64> shdrs;  // we normalise to 64-bit form for convenience
    shdrs.reserve(shnum);
    for (std::uint16_t i = 0; i < shnum; ++i) {
        const std::size_t off = shoff + static_cast<std::size_t>(i) * shentsize;
        if (off + shentsize > data.size()) NYX_THROW(Parser, "ELF: shdr out of bounds");
        Shdr64 s{};
        if (is_64) {
            s.name      = read_u32(off + 0);
            s.type      = read_u32(off + 4);
            s.flags     = read_u64(off + 8);
            s.addr      = read_u64(off + 16);
            s.offset    = read_u64(off + 24);
            s.size      = read_u64(off + 32);
            s.link      = read_u32(off + 40);
            s.info      = read_u32(off + 44);
            s.addralign = read_u64(off + 48);
            s.entsize   = read_u64(off + 56);
        } else {
            s.name      = read_u32(off + 0);
            s.type      = read_u32(off + 4);
            s.flags     = read_u32(off + 8);
            s.addr      = read_u32(off + 12);
            s.offset    = read_u32(off + 16);
            s.size      = read_u32(off + 20);
            s.link      = read_u32(off + 24);
            s.info      = read_u32(off + 28);
            s.addralign = read_u32(off + 32);
            s.entsize   = read_u32(off + 36);
        }
        shdrs.push_back(s);
    }

    // Section name string table
    auto read_str = [&](std::size_t strtab_off, std::uint32_t name_off) -> std::string {
        const std::size_t abs = strtab_off + name_off;
        if (abs >= data.size()) return {};
        const char* base = reinterpret_cast<const char*>(data.data());
        std::string out;
        for (std::size_t i = abs; i < data.size() && base[i] != '\0'; ++i) out.push_back(base[i]);
        return out;
    };

    std::size_t shstr_off = 0;
    if (shstrndx < shnum) shstr_off = shdrs[shstrndx].offset;

    for (const auto& s : shdrs) {
        Section sec{};
        sec.name      = read_str(shstr_off, s.name);
        sec.vaddr     = s.addr;
        sec.file_off  = s.offset;
        sec.file_size = s.size;
        sec.mem_size  = s.size;
        sec.flags     = static_cast<std::uint32_t>(s.flags & 0xFFFFFFFFu);
        sec.executable = (s.flags & SHF_EXECINSTR) != 0;
        sec.writable   = (s.flags & SHF_WRITE)     != 0;
        sec.readable   = true;
        sec.is_code    = (s.type == SHT_PROGBITS) && ((s.flags & SHF_EXECINSTR) != 0);
        info.sections.push_back(sec);
    }

    // Program headers: detect NX (PT_GNU_STACK without execute flag) and RELRO.
    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::size_t off = phoff + static_cast<std::size_t>(i) * phentsize;
        if (off + phentsize > data.size()) break;
        std::uint32_t p_type;
        std::uint32_t p_flags;
        if (is_64) {
            p_type  = read_u32(off + 0);
            p_flags = read_u32(off + 4);
        } else {
            p_type  = read_u32(off + 0);
            p_flags = read_u32(off + 24);
        }
        if (p_type == PT_GNU_STACK) {
            // PF_X = 1
            info.has_nx = ((p_flags & 0x1u) == 0u);
        } else if (p_type == PT_GNU_RELRO) {
            info.has_relro = true;
        }
    }

    // Symbol tables: walk .symtab and .dynsym
    auto parse_symtab = [&](const Shdr64& sh) {
        if (sh.entsize == 0 || sh.size == 0) return;
        if (sh.link >= shnum) return;
        const std::size_t strtab_off = shdrs[sh.link].offset;
        const std::size_t count = sh.size / sh.entsize;
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t off = sh.offset + i * sh.entsize;
            if (off + sh.entsize > data.size()) break;
            std::uint32_t name; std::uint8_t sym_info; std::uint16_t shndx;
            std::uint64_t value, size;
            if (is_64) {
                name  = read_u32(off + 0);
                sym_info  = data[off + 4];
                shndx = read_u16(off + 6);
                value = read_u64(off + 8);
                size  = read_u64(off + 16);
            } else {
                name  = read_u32(off + 0);
                value = read_u32(off + 4);
                size  = read_u32(off + 8);
                sym_info  = data[off + 12];
                shndx = read_u16(off + 14);
            }
            Symbol sym{};
            sym.name   = read_str(strtab_off, name);
            sym.value  = value;
            sym.size   = size;
            switch (st_type(sym_info)) {
                case STT_FUNC:    sym.kind = Symbol::Kind::Function; break;
                case STT_OBJECT:  sym.kind = Symbol::Kind::Object;   break;
                case STT_SECTION: sym.kind = Symbol::Kind::Section;  break;
                case STT_FILE:    sym.kind = Symbol::Kind::File;     break;
                default:           sym.kind = Symbol::Kind::Unknown;  break;
            }
            sym.imported = (shndx == 0);   // SHN_UNDEF
            sym.exported = (!sym.imported) && (sym.kind == Symbol::Kind::Function || sym.kind == Symbol::Kind::Object) && !sym.name.empty();
            if (!sym.name.empty()) info.symbols.push_back(sym);
        }
    };

    for (const auto& s : shdrs) {
        if (s.type == SHT_SYMTAB || s.type == SHT_DYNSYM) parse_symtab(s);
    }

    // Heuristic: stripped iff no .symtab and no symbol of kind Function.
    const bool has_func = std::any_of(info.symbols.begin(), info.symbols.end(),
                                       [](const Symbol& s){ return s.kind == Symbol::Kind::Function; });
    const bool has_symtab = std::any_of(shdrs.begin(), shdrs.end(),
                                         [](const Shdr64& s){ return s.type == SHT_SYMTAB; });
    info.stripped = !has_symtab || !has_func;

    return info;
}

}  // namespace nyx
