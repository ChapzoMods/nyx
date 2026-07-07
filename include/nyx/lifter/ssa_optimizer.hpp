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
    bool dead_store_elim   = true;  // v0.3.1: redundant Store-to-same-address
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

/// v0.3.1: Dead store elimination. Within a single basic block, if two
/// Store instructions write to the same constant address (Imm operand)
/// and no Load from that address or Call happens between them, the
/// first store is redundant and is removed. Stores through register
/// addresses are not modelled in v0.3.1 — only literal-address stores
/// are tracked. Returns the number of stores removed.
[[nodiscard]] std::size_t dead_store_elimination_pass(Function& fn);

/// v0.4.0: Simple interprocedural constant propagation.
/// Propagates constant arguments from call sites into called functions.
/// If a function is always called with the same constant argument,
/// that constant is substituted for the parameter inside the function.
[[nodiscard]] std::size_t interprocedural_constant_propagation(std::vector<Function>& fns);

}  // namespace nyx::ir
