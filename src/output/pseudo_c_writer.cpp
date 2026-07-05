// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/pseudo_c_writer.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"

#include <sstream>
#include <string>

namespace nyx::output {

void write_pseudo_c(std::ostream& os,
                    const BinaryInfo& bin,
                    const std::vector<DecompiledFunction>& functions) {
    os << "// =============================================================================\n";
    os << "// Nyx v0.0.1 - pseudo-C decompilation\n";
    os << "// Binary: " << bin.path << "\n";
    os << "// Format: " << to_string(bin.format) << "  Arch: " << arch_name(bin.arch) << "\n";
    os << "// Decompiled functions: " << functions.size() << "\n";
    os << "// =============================================================================\n\n";

    if (functions.empty()) {
        os << "// No functions were discovered.\n";
        os << "// Possible causes: stripped binary with no symbols, unsupported arch,\n";
        os << "// or the linear-sweep fallback was disabled in the Decompiler options.\n";
        return;
    }

    for (const auto& f : functions) {
        os << "// -----------------------------------------------------------------------------\n";
        os << "// Function: " << f.name << " @ " << to_hex(f.entry, 0, true) << "\n";
        os << "// " << f.block_count << " blocks, " << f.insn_count << " instructions\n";
        os << "// -----------------------------------------------------------------------------\n";
        for (const auto& line : f.lines) os << line << "\n";
        os << "\n";
    }
}

std::string to_pseudo_c(const BinaryInfo& bin,
                        const std::vector<DecompiledFunction>& functions) {
    std::ostringstream os;
    write_pseudo_c(os, bin, functions);
    return os.str();
}

}  // namespace nyx::output
