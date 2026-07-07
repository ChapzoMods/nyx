// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/lifter/ir.hpp"

#include <string>
#include <vector>

namespace nyx {

// Forward declaration - keeps the public header free of types.hpp.
struct BinaryInfo;

/// Short C operator for binary IR opcodes (e.g. Add -> "+"). Returns "?"
/// for non-binary opcodes.
[[nodiscard]] std::string_view op_name_short(ir::OpCode op) noexcept;

/// Renders a single IR function as pseudo-C source. The output is
/// deliberately conservative: every IR instruction becomes one C
/// statement, no SSA deconstruction, no type recovery. This keeps v0.0.1
/// honest about its capabilities and gives downstream tooling a stable
/// surface to consume.
[[nodiscard]] std::string render_pseudo_c(const ir::Function& fn);

/// v0.0.5: renders pseudo-C with dominator + loop information. When a
/// back edge is detected, the loop body is wrapped in a `while (1) { ... }`
/// block with the back edge rendered as `continue;` and any exit branch
/// rendered as `break;`. This produces structured `while`/`do-while`
/// constructs for simple counted loops.
[[nodiscard]] std::string render_pseudo_c(const ir::Function& fn,
                                          const ir::DominatorAnalysis& dom,
                                          const std::vector<ir::NaturalLoop>& loops);

/// Renders a single IR instruction as a C-like statement (no trailing newline).
[[nodiscard]] std::string render_instruction(const ir::Instruction& i);

/// Formats an operand as C source.
[[nodiscard]] std::string render_operand(const ir::Operand& o);

/// v0.2.1: set the BinaryInfo used for symbol resolution in render_instruction.
/// Pass nullptr to clear. The pointer is not retained beyond the next call.
void set_render_binary_info(const BinaryInfo* bin);

}  // namespace nyx
