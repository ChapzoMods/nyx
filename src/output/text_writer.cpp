// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/text_writer.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/parsers/dwarf_parser.hpp"
#include "nyx/version.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace nyx::output {

namespace {
std::string fmt_addr(std::uint64_t v) { return to_hex(v, 16, true); }
}

void write_text(std::ostream& os,
                const BinaryInfo& bin,
                const std::vector<std::vector<DecodedInstruction>>& sections) {
    os << "================================================================================\n";
    os << " Nyx v" << VERSION_STRING << " - text dump of " << bin.path << "\n";
    os << "================================================================================\n";
    os << " Format     : " << to_string(bin.format) << "\n";
    os << " Arch       : " << arch_pretty(bin.arch) << " (" << arch_name(bin.arch) << ")\n";
    os << " Endian     : " << (bin.endian == Endian::Little ? "little" : "big") << "\n";
    os << " 64-bit     : " << (bin.is_64bit ? "yes" : "no") << "\n";
    os << " PIE        : " << (bin.is_pie ? "yes" : "no") << "\n";
    os << " NX         : " << (bin.has_nx ? "yes" : "no") << "\n";
    os << " RELRO      : " << (bin.has_relro ? "yes" : "no") << "\n";
    os << " Stack canary: " << (bin.has_canary ? "yes" : "no") << "\n";
    os << " Stripped   : " << (bin.stripped ? "yes" : "no") << "\n";
    os << " Entry point: " << fmt_addr(bin.entry_point) << "\n";
    os << " Image base : " << fmt_addr(bin.image_base)  << "\n";

    os << "--------------------------------------------------------------------------------\n";
    os << " Sections (" << bin.sections.size() << "):\n";
    os << "  name              vaddr            file_off   file_size  flags\n";
    for (const auto& s : bin.sections) {
        std::string flags;
        if (s.executable) flags += 'x';
        if (s.writable)   flags += 'w';
        if (s.readable)   flags += 'r';
        if (s.is_code)    flags += 'c';
        os << "  " << std::left << std::setw(17) << s.name
           << " " << fmt_addr(s.vaddr)
           << " " << std::right << std::setw(10) << s.file_off
           << " " << std::setw(10) << s.file_size
           << " " << std::setw(4) << flags << "\n";
    }

    if (!bin.symbols.empty()) {
        os << "--------------------------------------------------------------------------------\n";
        os << " Symbols (" << bin.symbols.size() << "):\n";
        for (const auto& s : bin.symbols) {
            os << "  " << std::left << std::setw(35) << s.name
               << " " << fmt_addr(s.value)
               << "  size=" << std::dec << s.size
               << "  kind=";
            switch (s.kind) {
                case Symbol::Kind::Unknown:  os << "unk"; break;
                case Symbol::Kind::Function: os << "func"; break;
                case Symbol::Kind::Object:   os << "obj"; break;
                case Symbol::Kind::Section:  os << "sect"; break;
                case Symbol::Kind::File:     os << "file"; break;
            }
            if (s.imported) os << "  import";
            if (s.exported) os << "  export";
            os << "\n";
        }
    }

    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& insns = sections[i];
        if (insns.empty()) continue;
        os << "--------------------------------------------------------------------------------\n";
        os << " Disassembly [" << i << "] (" << insns.size() << " instructions):\n";
        for (const auto& in : insns) {
            os << "  " << fmt_addr(in.address) << "  ";
            // raw bytes (up to 8) in hex with zero-fill, two chars each
            for (std::size_t k = 0; k < in.bytes.size() && k < 8; ++k) {
                os << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(in.bytes[k]);
            }
            // pad the bytes column to fixed width 16 with spaces
            for (std::size_t k = in.bytes.size(); k < 8; ++k) os << "  ";
            // reset sticky formatting before printing mnemonic + op_str
            os << std::dec << std::setfill(' ') << std::left << std::setw(8) << in.mnemonic
               << " " << in.op_str;
            // v0.0.6: DWARF source annotation.
            if (bin.dwarf) {
                auto entry = bin.dwarf->lookup_address(in.address);
                if (entry) {
                    const auto fname = bin.dwarf->file_name(entry->file);
                    os << "  ; " << (fname.empty() ? std::string("?") : fname)
                       << ":" << entry->line;
                    if (entry->column > 0) os << ":" << entry->column;
                }
            }
            os << "\n";
        }
    }
    os << "================================================================================\n";
}

std::string to_text(const BinaryInfo& bin,
                    const std::vector<std::vector<DecodedInstruction>>& sections) {
    std::ostringstream os;
    write_text(os, bin, sections);
    return os.str();
}

}  // namespace nyx::output
