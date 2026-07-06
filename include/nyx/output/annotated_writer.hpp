// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/types.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace nyx::output {

/// v0.0.6: renders an annotated disassembly that interleaves source
/// lines (from DWARF .debug_line) with disassembled instructions,
/// similar to `objdump -S`. When no DWARF info is available, falls
/// back to a plain disassembly with `; file:line` suffixes.
void write_annotated(std::ostream& os,
                     const BinaryInfo& bin,
                     const std::vector<std::vector<DecodedInstruction>>& sections);

[[nodiscard]] std::string to_annotated(
    const BinaryInfo& bin,
    const std::vector<std::vector<DecodedInstruction>>& sections);

}  // namespace nyx::output
