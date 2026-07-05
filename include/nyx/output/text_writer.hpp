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

/// Writes a textual listing of the binary: header summary, sections,
/// symbols and a per-instruction dump of every executable section.
void write_text(std::ostream& os,
                const BinaryInfo& bin,
                const std::vector<std::vector<DecodedInstruction>>& sections);

[[nodiscard]] std::string to_text(const BinaryInfo& bin,
                                  const std::vector<std::vector<DecodedInstruction>>& sections);

}  // namespace nyx::output
