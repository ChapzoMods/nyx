// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace nyx::output {

/// Renders a single IR function's control-flow graph as a Graphviz DOT
/// digraph. Each basic block becomes a node whose label is the block's
/// start address and a one-line summary of every instruction. Edges are
/// labelled with the branch condition (when present) or "fall-through".
///
/// The output is suitable for piping to `dot -Tpng` or `dot -Tsvg`.
void write_dot(std::ostream& os, const ir::Function& fn);

/// v0.0.5: extended DOT output that annotates the graph with dominance
/// and loop information. Loop header nodes are coloured light yellow,
/// loop body nodes light blue, unreachable (pruned) nodes light grey,
/// and the entry node light green. Back edges are drawn with a dashed
/// style and labelled "back edge".
void write_dot(std::ostream& os, const ir::Function& fn,
               const ir::DominatorAnalysis& dom,
               const std::vector<ir::NaturalLoop>& loops);

/// Convenience overload: renders multiple functions as a single DOT
/// digraph with one cluster per function.
void write_dot(std::ostream& os, const std::vector<ir::Function>& fns);

[[nodiscard]] std::string to_dot(const ir::Function& fn);
[[nodiscard]] std::string to_dot(const ir::Function& fn,
                                 const ir::DominatorAnalysis& dom,
                                 const std::vector<ir::NaturalLoop>& loops);
[[nodiscard]] std::string to_dot(const std::vector<ir::Function>& fns);

}  // namespace nyx::output
