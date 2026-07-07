// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/lifter/ir.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nyx::ir {

/// v0.2.0: A structured region in the CFG — represents a high-level
/// control-flow construct (if/else, while, do-while, switch, or a
/// simple linear block).
struct Region {
    enum class Kind : std::uint8_t {
        Block,        // a single basic block
        IfThen,       // if (cond) { then }
        IfThenElse,   // if (cond) { then } else { else }
        While,        // while (cond) { body }
        DoWhile,      // do { body } while (cond)
        Switch,       // switch (cond) { case ... }
        Sequence,     // a linear sequence of regions
        Goto,         // irreducible — emit as goto
    };

    Kind kind = Kind::Block;
    std::size_t block_idx = 0;          // for Block: index into fn.blocks
    std::vector<std::unique_ptr<Region>> children;
    std::optional<VReg> condition;      // for If/While/DoWhile: the condition vreg
    std::uint64_t goto_target = 0;      // for Goto: target address

    [[nodiscard]] std::string kind_name() const noexcept;
};

/// Builds a structured region tree from the CFG + dominator analysis.
/// The algorithm uses a simplified interval-based approach:
///   1. Identify loops (from NaturalLoop) -> While/DoWhile regions.
///   2. Identify if/else patterns (BranchCond with diamond CFG).
///   3. Everything else falls back to Block or Goto.
///
/// Irreducible CFGs (multiple entry loops, irreducible forks) keep
/// their goto/label structure.
[[nodiscard]] std::unique_ptr<Region> structure_cfg(
    const Function& fn,
    const DominatorAnalysis& dom,
    const std::vector<NaturalLoop>& loops);

/// Renders a structured region tree as pseudo-C (without gotos where
/// possible). Replaces the basic-block-by-block rendering in
/// render_pseudo_c when SSA + structuring are enabled.
[[nodiscard]] std::string render_structured(
    const Function& fn,
    const Region& root);

}  // namespace nyx::ir
