// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/types.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx {

/// Maps a single decoded machine instruction to one or more IR
/// instructions. The v0.0.1 lifter is intentionally conservative:
///
///   * x86/x86-64 common forms (mov, add, sub, xor, push, pop, lea,
///     jmp, call, ret, jcc, cmp, test) are translated fully.
///   * ARM/AARCH64 data-processing and branch mnemonics are translated
///     using a mnemonic-string fallback (covers the common cases).
///   * PPC / MIPS use the generic mnemonic fallback (Opaque node).
///   * Anything not recognised becomes `OpCode::Opaque` so the downstream
///     decompiler never silently drops instructions.
///
/// The lifter keeps a per-function register map (machine reg -> vreg).
class InstructionLifter {
public:
    /// Function-pointer-like type used by the ARM64 operand parser to map
    /// register name strings (e.g. "x0", "w5", "sp") to VReg ids. Exposed
    /// as a public alias so the free function in the .cpp can use it
    /// without leaking the lifter's private state.
    using RegMapper = std::function<ir::VReg(const std::string&)>;

    InstructionLifter(Arch arch, Endian endian);

    /// Lifts a sequence of decoded instructions into IR. The first
    /// instruction's address becomes the function's entry.
    [[nodiscard]] ir::Function lift_function(
        const std::vector<DecodedInstruction>& insns,
        std::string name = {},
        std::uint64_t forced_entry = 0) const;

    /// Lifts a single instruction. Exposed for unit tests.
    [[nodiscard]] std::vector<ir::Instruction> lift_one(const DecodedInstruction& insn) const;

private:
    Arch    arch_;
    Endian  endian_;

    // Per-function vreg allocator state - reset on every lift_function call.
    // Kept mutable here so lift_one (logically const) can re-use the same
    // machinery without exposing allocation state to callers.
    mutable std::unordered_map<std::uint32_t, ir::VReg> reg_map_;
    mutable ir::VReg next_vreg_ = 1;

    [[nodiscard]] ir::VReg map_reg(std::uint32_t machine_reg) const;
    /// Maps a register name (e.g. "rax", "x0", "sp") to a stable VReg.
    /// Two calls with the same name return the same vreg.
    [[nodiscard]] ir::VReg map_reg_by_name(const std::string& name) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_x86(const DecodedInstruction& insn) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_arm64(const DecodedInstruction& insn) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_arm32(const DecodedInstruction& insn) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_ppc(const DecodedInstruction& insn) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_mips(const DecodedInstruction& insn) const;
    [[nodiscard]] std::vector<ir::Instruction> lift_generic(const DecodedInstruction& insn) const;
};

}  // namespace nyx
