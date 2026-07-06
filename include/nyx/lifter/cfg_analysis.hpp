// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nyx::ir {

/// Result of running dominance analysis on a Function's CFG.
///
/// The dominator tree is computed with the iterative dataflow algorithm
/// (Cooper / Harvey / Kennedy 2001) rather than Lengauer-Tarjan. The
/// asymptotic complexity is O(n^2) but for the function sizes Nyx
/// handles (typically < 1000 blocks) the constant factor wins over
/// Lengauer-Tarjan's O(n α(n)) with its more complex data structures.
struct DominatorAnalysis {
    /// idom[b] = index of the immediate dominator of block b, or -1 if b
    /// is the entry block (or unreachable).
    std::vector<int> idom;

    /// Reachable set, in block-index form. Unreachable blocks are NOT in
    /// this vector. Order is reverse-postorder (used by the algorithm).
    std::vector<std::size_t> rpo;

    /// Returns true if block `b` is reachable from the entry.
    [[nodiscard]] bool reachable(std::size_t b) const noexcept {
        return idom[b] >= -1 && std::find(rpo.begin(), rpo.end(), b) != rpo.end();
    }

    /// Returns the immediate dominator of block b, or nullopt if b is the
    /// entry block or unreachable.
    [[nodiscard]] std::optional<std::size_t> immediate_dominator(std::size_t b) const noexcept;

    /// Returns true if `a` dominates `b` (a is on the path from entry to b).
    [[nodiscard]] bool dominates(std::size_t a, std::size_t b) const noexcept;
};

/// A natural loop in the CFG, identified by a back edge (header -> latch).
struct NaturalLoop {
    std::size_t header = 0;      // block that dominates the latch
    std::size_t latch  = 0;      // block whose successor is the header
    std::vector<std::size_t> body;  // all blocks in the loop (including header & latch)
};

/// Computes the dominator tree for `fn`. The function's entry is taken
/// from `fn.entry`; the entry block is found by looking up its address in
/// `fn.block_index`.
[[nodiscard]] DominatorAnalysis compute_dominators(const Function& fn);

/// Identifies all natural loops in `fn` given a precomputed dominator
/// analysis. A back edge is an edge B -> H where H dominates B; the loop
/// body is the set of blocks that can reach B without going through H,
/// plus H itself.
[[nodiscard]] std::vector<NaturalLoop> find_natural_loops(
    const Function& fn,
    const DominatorAnalysis& dom);

/// Returns the set of block indices reachable from the entry. Useful for
/// pruning unreachable blocks.
[[nodiscard]] std::unordered_set<std::size_t> reachable_blocks(const Function& fn);

}  // namespace nyx::ir
