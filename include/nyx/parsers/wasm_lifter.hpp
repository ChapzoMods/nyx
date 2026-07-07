// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once
#include "nyx/lifter/cfg.hpp"
#include "nyx/parsers/wasm_parser.hpp"

namespace nyx {

/// Lifts WASM bytecodes to Nyx IR. The lifter is deliberately small: it
/// handles the common numeric / control-flow bytecodes (local.get,
/// local.set, i32.const, i32.add/sub/mul, call, return, end, br, br_if)
/// and falls back to `OpCode::Opaque` for anything else.
///
/// Locals are mapped 1:1 to vregs: WASM local N becomes vreg N+1 (vreg 0
/// is reserved as the INVALID_VREG sentinel). Intermediate values from
/// the WASM value stack are allocated fresh vregs starting at
/// (local_count + 1) + 1.
[[nodiscard]] ir::Function lift_wasm_function(const WasmFuncBody& body,
                                              const WasmFuncType& type,
                                              std::string name);

}  // namespace nyx
