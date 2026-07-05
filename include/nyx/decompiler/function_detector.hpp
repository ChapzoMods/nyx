// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <cstdint>
#include <vector>

namespace nyx {

/// Heuristic function-entry detector for stripped binaries.
///
/// When the symbol table is missing (or doesn't list any function), the
/// decompiler can't rely on `BinaryInfo::symbols` to find function starts.
/// FunctionDetector scans the disassembled code looking for well-known
/// prologue patterns:
///
///   x86 / x86-64:  push rbp ; mov rbp, rsp
///                  push rbx ; ...
///                  sub rsp, imm
///                  endbr64 ; push rbp
///   ARM 32:        push {fp, lr}  /  push {r4-r11, lr}
///   AArch64:       stp x29, x30, [sp, #...]  /  stp fp, lr
///   PowerPC:       stwu r1, -imm(r1)  /  mflr r0
///   MIPS:          addiu sp, sp, -imm  /  sw ra, imm(sp)
///
/// Each match is returned as a Candidate with its address. Callers then
/// disassemble forward from each candidate until the next candidate or the
/// end of the section.
class FunctionDetector {
public:
    struct Candidate {
        std::uint64_t address;
        std::string   name;  // "sub_<hex>" placeholder
    };

    explicit FunctionDetector(Arch arch);

    /// Scans `insns` (a linear sweep of a code section) and returns every
    /// instruction address that looks like a function prologue start.
    [[nodiscard]] std::vector<Candidate> detect(const std::vector<DecodedInstruction>& insns) const;

    /// v0.0.4: returns the index of the next "function end" instruction
    /// at or after `start_idx` in `insns`. A function end is a return
    /// instruction (ret / blr / jr $ra / bx lr / leave+ret) followed by
    /// either padding or a prologue. Returns insns.size() if no end is
    /// found. Callers use this to bound the body of a function detected
    /// by `detect()`.
    [[nodiscard]] std::size_t find_function_end(const std::vector<DecodedInstruction>& insns,
                                                 std::size_t start_idx) const;

private:
    Arch arch_;

    [[nodiscard]] bool is_x86_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const;
    [[nodiscard]] bool is_arm64_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const;
    [[nodiscard]] bool is_arm32_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const;
    [[nodiscard]] bool is_ppc_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const;
    [[nodiscard]] bool is_mips_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const;

    /// v0.0.4: returns true if the instruction is a function-return
    /// terminator for the configured architecture.
    [[nodiscard]] bool is_return(const DecodedInstruction& insn) const;
};

}  // namespace nyx
