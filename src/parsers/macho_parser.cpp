// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/parsers/macho_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace nyx {

namespace {

constexpr std::uint32_t MH_MAGIC      = 0xFEEDFACEu;
constexpr std::uint32_t MH_MAGIC_64   = 0xFEEDFACFu;
constexpr std::uint32_t FAT_MAGIC     = 0xCAFEBABEu;
constexpr std::uint32_t FAT_MAGIC_64  = 0xCAFEBABFu;

// cputype values
constexpr std::int32_t CPU_TYPE_X86       = 7;
constexpr std::int32_t CPU_TYPE_X86_64    = 7 | 0x01000000;
constexpr std::int32_t CPU_TYPE_ARM       = 12;
constexpr std::int32_t CPU_TYPE_ARM64     = 12 | 0x01000000;
constexpr std::int32_t CPU_TYPE_POWERPC   = 18;
constexpr std::int32_t CPU_TYPE_POWERPC64 = 18 | 0x01000000;

// load commands
constexpr std::uint32_t LC_SEGMENT       = 0x1u;
constexpr std::uint32_t LC_SEGMENT_64    = 0x19u;
constexpr std::uint32_t LC_SYMTAB        = 0x2u;
constexpr std::uint32_t LC_DYSYMTAB      = 0xBu;
constexpr std::uint32_t LC_UUID          = 0x1Bu;
constexpr std::uint32_t LC_MAIN          = 0x80000028u;

// n_type bits
constexpr std::uint8_t N_STAB = 0xE0;
constexpr std::uint8_t N_TYPE = 0x0E;
constexpr std::uint8_t N_EXT  = 0x01;
constexpr std::uint8_t N_SECT = 0x0E;

// section flags - high 8 bits of flags are section type
constexpr std::uint32_t SECTION_TYPE_MASK = 0x000000FFu;
constexpr std::uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000u;
constexpr std::uint32_t S_ATTR_SOME_INSTRUCTIONS = 0x00004000u;

struct MachHeader {
    std::uint32_t magic;
    std::int32_t  cputype;
    std::int32_t  cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;  // only valid for 64-bit
};

struct SegmentCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char         segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct Section32 {
    char          sectname[16];
    char          segname[16];
    std::uint32_t addr;
    std::uint32_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};

struct Section64 {
    char          sectname[16];
    char          segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

struct Nlist32 {
    std::uint32_t n_strx;
    std::uint8_t  n_type;
    std::uint8_t  n_sect;
    std::int16_t  n_desc;
    std::uint32_t n_value;
};

struct Nlist64 {
    std::uint32_t n_strx;
    std::uint8_t  n_type;
    std::uint8_t  n_sect;
    std::int16_t  n_desc;
    std::uint64_t n_value;
};

std::string fixed_str(const char* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != '\0') ++len;
    return std::string(p, len);
}

}  // namespace

bool MachOParser::accepts(ByteView magic) const noexcept {
    if (magic.size() < 4) return false;
    const std::uint32_t be = read_u32_be(magic.data());
    const std::uint32_t le = read_u32_le(magic.data());
    return be == MH_MAGIC || be == MH_MAGIC_64
        || le == MH_MAGIC || le == MH_MAGIC_64
        || be == FAT_MAGIC || be == FAT_MAGIC_64
        || le == FAT_MAGIC || le == FAT_MAGIC_64;
}

BinaryInfo MachOParser::parse(ByteView data) const {
    if (!accepts(data)) NYX_THROW(Parser, "Mach-O: bad magic");
    if (data.size() < 4) NYX_THROW(Parser, "Mach-O: too small");

    const std::uint32_t be = read_u32_be(data.data());
    const std::uint32_t le = read_u32_le(data.data());

    if (be == FAT_MAGIC || be == FAT_MAGIC_64 || le == FAT_MAGIC || le == FAT_MAGIC_64) {
        // Fat binary: pick the first slice that maps to a known arch.
        const bool swap = (be == FAT_MAGIC || be == FAT_MAGIC_64);
        const std::uint32_t nfat = swap ? read_u32_be(data.data() + 4) : read_u32_le(data.data() + 4);
        for (std::uint32_t i = 0; i < nfat; ++i) {
            const std::size_t off = 8 + i * 20;
            if (off + 20 > data.size()) break;
            const std::int32_t cputype = swap ? static_cast<std::int32_t>(read_u32_be(data.data() + off)) : static_cast<std::int32_t>(read_u32_le(data.data() + off));
            const std::uint32_t slice_off = swap ? read_u32_be(data.data() + off + 8) : read_u32_le(data.data() + off + 8);
            const std::uint32_t slice_size = swap ? read_u32_be(data.data() + off + 12) : read_u32_le(data.data() + off + 12);
            if (slice_off + slice_size > data.size()) continue;
            // Recurse on the slice.
            MachOParser inner;
            ByteView slice{data.data() + slice_off, slice_size};
            if (!inner.accepts(slice)) continue;
            // Only accept slices for arches Nyx understands.
            switch (cputype) {
                case CPU_TYPE_X86: case CPU_TYPE_X86_64:
                case CPU_TYPE_ARM: case CPU_TYPE_ARM64:
                case CPU_TYPE_POWERPC: case CPU_TYPE_POWERPC64:
                    break;
                default: continue;
            }
            BinaryInfo info = inner.parse(slice);
            NYX_INFO("Mach-O: selected slice " + std::to_string(i) + " (cputype=" + std::to_string(cputype) + ")");
            return info;
        }
        NYX_THROW(Parser, "Mach-O: fat archive contains no recognised slice");
    }

    // Single-arch Mach-O. Endianness: magic in BE means host-swap needed.
    const bool is_64 = (be == MH_MAGIC_64 || le == MH_MAGIC_64);
    const bool swap  = (be == MH_MAGIC || be == MH_MAGIC_64);  // big-endian magic on disk
    const Endian endian = swap ? Endian::Big : Endian::Little;
    const bool le_reader = !swap;

    auto r16 = [&](std::size_t o) { return le_reader ? read_u16_le(data.data() + o) : read_u16_be(data.data() + o); };
    auto r32 = [&](std::size_t o) { return le_reader ? read_u32_le(data.data() + o) : read_u32_be(data.data() + o); };
    auto r64 = [&](std::size_t o) { return le_reader ? read_u64_le(data.data() + o) : read_u64_be(data.data() + o); };
    auto rs32 = [&](std::size_t o) { return static_cast<std::int32_t>(r32(o)); };

    if (data.size() < 28) NYX_THROW(Parser, "Mach-O: header too small");
    MachHeader h{};
    h.magic      = r32(0);
    h.cputype    = rs32(4);
    h.cpusubtype = rs32(8);
    h.filetype   = r32(12);
    h.ncmds      = r32(16);
    h.sizeofcmds = r32(20);
    h.flags      = r32(24);
    h.reserved   = is_64 ? r32(28) : 0;

    BinaryInfo info{};
    info.format   = BinaryFormat::MachO;
    info.endian   = endian;
    info.is_64bit = is_64;
    info.stripped = (h.flags & 0x2u) != 0;  // MH_NOMULTIDEFS bit is irrelevant; we approximate with the SF_NOUNDEFS bit 0x1 actually. Use 0x200 (MH_PIE) for pie check below.
    info.is_pie   = (h.flags & 0x2000u) != 0;  // MH_PIE

    switch (h.cputype) {
        case CPU_TYPE_X86:       info.arch = Arch::X86;      break;
        case CPU_TYPE_X86_64:    info.arch = Arch::X86_64;   break;
        case CPU_TYPE_ARM:       info.arch = Arch::ARM;      break;
        case CPU_TYPE_ARM64:     info.arch = Arch::AARCH64;  break;
        case CPU_TYPE_POWERPC:   info.arch = Arch::PPC;      break;
        case CPU_TYPE_POWERPC64: info.arch = Arch::PPC64;    break;
        default:
            info.arch = Arch::Unknown;
            NYX_WARN("Mach-O: unhandled cputype=" + std::to_string(h.cputype));
            break;
    }

    // Walk load commands.
    const std::size_t header_size = is_64 ? 32 : 28;
    std::size_t off = header_size;
    std::uint64_t symtab_off = 0, symtab_nsyms = 0, strtab_off = 0, strtab_size = 0;

    for (std::uint32_t i = 0; i < h.ncmds; ++i) {
        if (off + 8 > data.size()) NYX_THROW(Parser, "Mach-O: load command header OOB");
        const std::uint32_t cmd     = r32(off);
        const std::uint32_t cmdsize = r32(off + 4);
        if (cmdsize < 8 || off + cmdsize > data.size()) NYX_THROW(Parser, "Mach-O: bad load cmd size");

        if (cmd == LC_SEGMENT || cmd == LC_SEGMENT_64) {
            const bool seg64 = (cmd == LC_SEGMENT_64);
            const char* segname = reinterpret_cast<const char*>(data.data() + off + 8);
            const std::uint64_t vmaddr   = seg64 ? r64(off + 24) : r32(off + 24);
            const std::uint64_t vmsize   = seg64 ? r64(off + 32) : r32(off + 28);
            const std::uint64_t fileoff  = seg64 ? r64(off + 40) : r32(off + 32);
            const std::uint64_t filesize = seg64 ? r64(off + 48) : r32(off + 36);
            const std::uint32_t maxprot  = seg64 ? r32(off + 56) : r32(off + 40);
            const std::uint32_t nsects   = seg64 ? r32(off + 64) : r32(off + 48);
            const std::size_t sect_hdr   = off + (seg64 ? 72 : 56);
            const std::size_t sect_sz    = seg64 ? sizeof(Section64) : sizeof(Section32);

            // vm_prot constants: READ=1, WRITE=2, EXECUTE=4
            (void)vmsize; (void)fileoff; (void)filesize; (void)maxprot;

            for (std::uint32_t s = 0; s < nsects; ++s) {
                const std::size_t so = sect_hdr + s * sect_sz;
                if (so + sect_sz > data.size()) break;
                Section sec{};
                if (seg64) {
                    const Section64* p = reinterpret_cast<const Section64*>(data.data() + so);
                    sec.name      = fixed_str(p->sectname, 16);
                    sec.vaddr     = p->addr;
                    sec.file_off  = p->offset;
                    sec.file_size = p->size;
                    sec.mem_size  = p->size;
                    sec.flags     = p->flags;
                } else {
                    const Section32* p = reinterpret_cast<const Section32*>(data.data() + so);
                    sec.name      = fixed_str(p->sectname, 16);
                    sec.vaddr     = p->addr;
                    sec.file_off  = p->offset;
                    sec.file_size = p->size;
                    sec.mem_size  = p->size;
                    sec.flags     = p->flags;
                }
                // Executable if section type is S_REGULAR with the pure-instructions attribute,
                // OR section name matches __text in __TEXT segment.
                sec.executable = (segname == std::string_view("__TEXT")) && (sec.name == "__text" || sec.name == "__stubs" || sec.name == "__stub_helper");
                sec.readable   = (maxprot & 0x1) != 0;
                sec.writable   = (maxprot & 0x2) != 0;
                sec.is_code    = sec.executable || ((sec.flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS)) != 0);
                info.sections.push_back(sec);
            }
        } else if (cmd == LC_SYMTAB) {
            symtab_off   = r32(off + 8);
            symtab_nsyms = r32(off + 12);
            strtab_off   = r32(off + 16);
            strtab_size  = r32(off + 20);
        } else if (cmd == LC_MAIN) {
            // entryoff is a file offset relative to image base (which is 0
            // for Mach-O). We can't compute the VA until we know __TEXT.vmaddr,
            // which we usually do by the time we get here. For v0.0.1 we
            // stash the file offset and resolve it post-loop.
            const std::uint64_t entryoff = r64(off + 8);
            info.entry_point = entryoff;  // resolved to VA below
        }

        off += cmdsize;
    }

    // If we have an LC_MAIN entry that's a file offset, convert to VA.
    if (info.entry_point != 0) {
        for (const auto& s : info.sections) {
            if (s.file_off != 0 && info.entry_point >= s.file_off && info.entry_point < s.file_off + s.file_size) {
                info.entry_point = s.vaddr + (info.entry_point - s.file_off);
                break;
            }
        }
    }

    // Symbols from LC_SYMTAB
    if (symtab_off && symtab_nsyms && strtab_off) {
        const std::size_t sym_size = is_64 ? sizeof(Nlist64) : sizeof(Nlist32);
        if (symtab_off + symtab_nsyms * sym_size <= data.size()) {
            for (std::uint64_t i = 0; i < symtab_nsyms; ++i) {
                const std::size_t so = symtab_off + i * sym_size;
                std::uint32_t strx; std::uint8_t n_type; std::uint8_t n_sect; std::uint64_t n_value;
                if (is_64) {
                    const Nlist64* p = reinterpret_cast<const Nlist64*>(data.data() + so);
                    strx = p->n_strx; n_type = p->n_type; n_sect = p->n_sect; n_value = p->n_value;
                } else {
                    const Nlist32* p = reinterpret_cast<const Nlist32*>(data.data() + so);
                    strx = p->n_strx; n_type = p->n_type; n_sect = p->n_sect; n_value = p->n_value;
                }
                if ((n_type & N_STAB) != 0) continue;  // debug symbol
                const std::uint8_t type = n_type & N_TYPE;
                if (type != N_SECT && type != 0x01 /*N_EXT absolute*/) continue;
                if (strx == 0 || strtab_off + strx >= data.size()) continue;

                const char* base = reinterpret_cast<const char*>(data.data()) + strtab_off;
                std::string nm;
                for (std::size_t j = strx; j + strtab_off < data.size() && base[j] != '\0'; ++j) nm.push_back(base[j]);
                if (nm.empty()) continue;

                Symbol sym{};
                sym.name  = nm;
                sym.value = n_value;
                sym.kind  = (n_sect == 1) ? Symbol::Kind::Function : Symbol::Kind::Unknown;
                sym.imported = (type == 0x00);  // undefined symbol
                sym.exported = (n_type & N_EXT) != 0 && !sym.imported;
                info.symbols.push_back(sym);
            }
        }
    }

    return info;
}

}  // namespace nyx
