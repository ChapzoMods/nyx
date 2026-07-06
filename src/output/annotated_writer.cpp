// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/annotated_writer.hpp"

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
}  // namespace

void write_annotated(std::ostream& os,
                     const BinaryInfo& bin,
                     const std::vector<std::vector<DecodedInstruction>>& sections) {
    os << "================================================================================\n";
    os << " Nyx v" << VERSION_STRING << " - annotated disassembly of " << bin.path << "\n";
    os << "================================================================================\n";
    os << " Format     : " << to_string(bin.format) << "\n";
    os << " Arch       : " << arch_pretty(bin.arch) << " (" << arch_name(bin.arch) << ")\n";
    os << " DWARF      : " << (bin.dwarf ? "available" : "not available") << "\n";
    os << "================================================================================\n\n";

    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& insns = sections[i];
        if (insns.empty()) continue;

        os << "=== Section " << i << " ===\n";

        // Track the last source file:line we emitted so we only print
        // source context when the line changes.
        std::uint32_t last_file = 0;
        std::uint32_t last_line = 0;

        for (const auto& in : insns) {
            // If we have DWARF info and the line changed, emit a source
            // context comment.
            if (bin.dwarf) {
                auto entry = bin.dwarf->lookup_address(in.address);
                if (entry && (entry->file != last_file || entry->line != last_line)) {
                    const auto fname = bin.dwarf->file_name(entry->file);
                    os << "\n  // " << (fname.empty() ? std::string("?") : fname)
                       << ":" << entry->line << "\n";
                    last_file = entry->file;
                    last_line = entry->line;
                }
            }

            os << "  " << fmt_addr(in.address) << "  ";
            for (std::size_t k = 0; k < in.bytes.size() && k < 8; ++k) {
                os << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(in.bytes[k]);
            }
            for (std::size_t k = in.bytes.size(); k < 8; ++k) os << "  ";
            os << std::dec << std::setfill(' ') << std::left << std::setw(8) << in.mnemonic
               << " " << in.op_str;

            // Inline annotation.
            if (bin.dwarf) {
                auto entry = bin.dwarf->lookup_address(in.address);
                if (entry) {
                    os << "  ; :" << entry->line;
                    if (entry->column > 0) os << ":" << entry->column;
                }
            }
            os << "\n";
        }
        os << "\n";
    }
    os << "================================================================================\n";
}

std::string to_annotated(const BinaryInfo& bin,
                         const std::vector<std::vector<DecodedInstruction>>& sections) {
    std::ostringstream os;
    write_annotated(os, bin, sections);
    return os.str();
}

}  // namespace nyx::output
