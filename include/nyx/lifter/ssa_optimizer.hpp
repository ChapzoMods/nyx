// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"

#include <cstdint>

namespace nyx::ir {

/// v0.2.0: SSA-level optimizations applied to a Function.
///
/// All passes are safe (preserve semantics) and operate on the IR
/// in-place. They are enabled with the `-O1` CLI flag.
struct OptimizationOptions {
    bool constant_folding  = true;  // fold arithmetic on immediates
    bool dead_code_elim    = true;  // remove stores/phis whose result is unused
    bool expr_simplification = true; // v1 + 0 -> v1, v1 * 1 -> v1, etc.
};

/// Runs all enabled optimization passes on `fn` until no more changes
/// are observed (fixpoint). Returns the number of instructions removed.
[[nodiscard]] std::size_t optimize(Function& fn, const OptimizationOptions& opts = {});

/// Constant folding: evaluates arithmetic instructions where both
/// operands are immediates and replaces the result with a Mov of the
/// computed value.
[[nodiscard]] std::size_t constant_folding_pass(Function& fn);

/// Dead code elimination: removes instructions whose destination vreg
/// is never used (and that have no side effects, i.e. not Store/Call/
/// Branch/Return/Push).
[[nodiscard]] std::size_t dead_code_elimination_pass(Function& fn);

/// Expression simplification: replaces patterns like `v1 + 0`, `v1 * 1`,
/// `v1 - 0`, `v1 | 0`, `v1 & 0xFF..FF` with simpler forms.
[[nodiscard]] std::size_t expression_simplification_pass(Function& fn);

}  // namespace nyx::ir
