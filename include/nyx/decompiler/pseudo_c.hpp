// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"

#include <string>

namespace nyx {

/// Short C operator for binary IR opcodes (e.g. Add -> "+"). Returns "?"
/// for non-binary opcodes.
[[nodiscard]] std::string_view op_name_short(ir::OpCode op) noexcept;

/// Renders a single IR function as pseudo-C source. The output is
/// deliberately conservative: every IR instruction becomes one C
/// statement, no SSA deconstruction, no type recovery. This keeps v0.0.1
/// honest about its capabilities and gives downstream tooling a stable
/// surface to consume.
[[nodiscard]] std::string render_pseudo_c(const ir::Function& fn);

/// Renders a single IR instruction as a C-like statement (no trailing newline).
[[nodiscard]] std::string render_instruction(const ir::Instruction& i);

/// Formats an operand as C source.
[[nodiscard]] std::string render_operand(const ir::Operand& o);

}  // namespace nyx
