// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/lifter/ir.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nyx::ir {

/// v0.1.0: SSA construction result.
///
/// `build_ssa` transforms a function's IR into Static Single Assignment
/// form by:
///   1. Computing dominance frontiers from the dominator tree.
///   2. Inserting phi instructions at join points (dominance frontier
///      of every definition site).
///   3. Renaming variables via a depth-first walk of the dominator tree,
///      using a stack per original variable to track the current SSA
///      version.
///
/// The returned function has the same CFG structure but every vreg
/// definition gets a unique SSA version, and phi nodes appear at the
/// start of blocks where multiple definitions meet.
struct SSAResult {
    /// The SSA-transformed function.
    Function fn;

    /// Maps original vreg -> list of SSA versions created.
    std::unordered_map<VReg, std::vector<VReg>> versions;

    /// Maps SSA vreg -> original vreg (for debugging / rendering).
    std::unordered_map<VReg, VReg> original;
};

/// Builds SSA form for `fn` using the precomputed dominator analysis.
/// The input function is not modified; a new SSAResult is returned.
[[nodiscard]] SSAResult build_ssa(const Function& fn, const DominatorAnalysis& dom);

/// Computes the dominance frontier for every block in the CFG.
/// `df[i]` is the set of block indices in the dominance frontier of
/// block `i`. Uses the standard algorithm: for each join block with
/// multiple predecessors, walk up the dominator tree from each
/// predecessor until reaching the immediate dominator, adding the
/// block to the frontier of each node on the path.
[[nodiscard]] std::vector<std::unordered_set<std::size_t>>
    compute_dominance_frontiers(const Function& fn, const DominatorAnalysis& dom);

}  // namespace nyx::ir
