// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/function_detector.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <string>

namespace nyx {

FunctionDetector::FunctionDetector(Arch arch) : arch_(arch) {}

bool FunctionDetector::is_x86_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    // endbr64 ; push rbp ; mov rbp, rsp
    // push rbp ; mov rbp, rsp
    // push rbx ; ...
    // sub rsp, imm
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;

    if (m == "endbr64" && second) {
        const std::string& m2 = second->mnemonic;
        if (m2 == "push") return true;
    }
    if (m == "push") {
        // push rbp / push ebp / push rbx / push esp
        if (o == "rbp" || o == "ebp" || o == "rbx" || o == "ebx") return true;
    }
    if (m == "sub") {
        // sub rsp, imm  /  sub esp, imm
        if (o.find("rsp,") == 0 || o.find("esp,") == 0) return true;
    }
    if (m == "mov" && second) {
        // mov rbp, rsp is usually the second instruction of a prologue,
        // NOT a function start. We only treat it as a prologue if it's the
        // very first instruction of the section (no previous instruction to
        // combine with). The detector doesn't know "first in section", so
        // we deliberately DON'T flag standalone mov rbp,rsp here.
    }
    return false;
}

bool FunctionDetector::is_arm64_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // stp x29, x30, [sp, ...]  /  stp fp, lr, [sp, ...]
    if (m == "stp") {
        if (o.find("x29, x30") != std::string::npos) return true;
        if (o.find("fp, lr") != std::string::npos)   return true;
    }
    // sub sp, sp, #imm  (stack frame allocation)
    if (m == "sub" && o.find("sp, sp") != std::string::npos) return true;
    // mov x29, sp  (frame pointer setup)
    if (m == "mov" && o.find("x29, sp") != std::string::npos) return true;
    return false;
}

bool FunctionDetector::is_arm32_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // push {fp, lr}  /  push {r4-r11, lr}  /  push {lr}
    if (m == "push" && o.find("lr") != std::string::npos) return true;
    // sub sp, sp, #imm
    if (m == "sub" && o.find("sp, sp") != std::string::npos) return true;
    // mov fp, sp  /  mov r11, sp
    if (m == "mov" && (o.find("fp, sp") != std::string::npos || o.find("r11, sp") != std::string::npos)) return true;
    // stmfd sp!, {fp, lr, ...}  - ARM32 store-multiple full-descending
    if (m == "stmfd" && o.find("lr") != std::string::npos) return true;
    // str lr, [sp, #-imm]!  - save LR with pre-decrement (alt prologue)
    if (m == "str" && o.find("lr") != std::string::npos && o.find("sp") != std::string::npos) return true;
    // add ip, sp, #imm  /  add r12, sp, #imm  (used by PIC prologues)
    if (m == "add" && (o.find("ip, sp") != std::string::npos || o.find("r12, sp") != std::string::npos)) return true;
    return false;
}

bool FunctionDetector::is_ppc_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // stwu r1, -imm(r1)  - canonical PPC prologue
    if (m == "stwu" && o.find("r1") != std::string::npos && o.find("(r1)") != std::string::npos) return true;
    // mflr r0  - often precedes stwu (saves LR)
    if (m == "mflr" && (o == "r0" || o == "0")) return true;
    // stw r0, imm(r1)  - save LR after mflr
    if (m == "stw" && o.find("r0") != std::string::npos && o.find("(r1)") != std::string::npos) return true;
    // stwu r1, -imm(r1) variants with different register numbers
    if (m == "stwu" && o.find("(r1)") != std::string::npos) return true;
    // stfd (save FPR) - sometimes appears in prologue
    if (m == "stfd" && o.find("(r1)") != std::string::npos) return true;
    return false;
}

bool FunctionDetector::is_mips_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // addiu sp, sp, -imm  (stack frame allocation; the negative immediate is the clue)
    if (m == "addiu" && o.find("$sp,") != std::string::npos) {
        if (o.find(", -") != std::string::npos) return true;
    }
    // sw ra, imm(sp)  - save return address
    if (m == "sw" && o.find("$ra") != std::string::npos) return true;
    // sw fp, imm(sp)  - save frame pointer
    if (m == "sw" && o.find("$fp") != std::string::npos) return true;
    // sw gp, imm(sp)  - save global pointer (PIC prologue)
    if (m == "sw" && o.find("$gp") != std::string::npos) return true;
    // daddiu sp, sp, -imm  (MIPS64 stack frame)
    if (m == "daddiu" && o.find("$sp,") != std::string::npos && o.find(", -") != std::string::npos) return true;
    // sd ra, imm(sp)  - MIPS64 doubleword save
    if (m == "sd" && o.find("$ra") != std::string::npos) return true;
    // lui gp, %hi(...)  - PIC prologue setup
    if (m == "lui" && o.find("$gp") != std::string::npos) return true;
    return false;
}

std::vector<FunctionDetector::Candidate> FunctionDetector::detect(const std::vector<DecodedInstruction>& insns) const {
    std::vector<Candidate> out;
    if (insns.empty()) return out;

    for (std::size_t i = 0; i < insns.size(); ++i) {
        const DecodedInstruction* second = (i + 1 < insns.size()) ? &insns[i + 1] : nullptr;
        bool is_prologue = false;
        switch (arch_) {
            case Arch::X86:
            case Arch::X86_64:  is_prologue = is_x86_prologue(insns[i], second); break;
            case Arch::AARCH64: is_prologue = is_arm64_prologue(insns[i], second); break;
            case Arch::ARM:     is_prologue = is_arm32_prologue(insns[i], second); break;
            case Arch::PPC:
            case Arch::PPC64:   is_prologue = is_ppc_prologue(insns[i], second); break;
            case Arch::MIPS:
            case Arch::MIPS64:  is_prologue = is_mips_prologue(insns[i], second); break;
            default: break;
        }
        if (is_prologue) {
            Candidate c;
            c.address = insns[i].address;
            c.name    = "sub_" + to_hex(c.address, 0, false);
            out.push_back(c);
        }
    }

    // Deduplicate (the same address can be flagged by multiple patterns).
    std::sort(out.begin(), out.end(),
              [](const Candidate& a, const Candidate& b){ return a.address < b.address; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const Candidate& a, const Candidate& b){ return a.address == b.address; }),
              out.end());

    NYX_INFO("FunctionDetector: found " + std::to_string(out.size()) + " prologue candidates");
    return out;
}

bool FunctionDetector::is_return(const DecodedInstruction& insn) const {
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;

    switch (arch_) {
        case Arch::X86:
        case Arch::X86_64:
            // ret / retn / retq, or leave (which often precedes ret).
            if (m == "ret" || m == "retn" || m == "retq") return true;
            return false;
        case Arch::AARCH64:
            // ret / retaa / retab
            if (m == "ret" || m == "retaa" || m == "retab") return true;
            return false;
        case Arch::ARM:
            // bx lr / pop {... pc} / ldr pc, [sp], #4
            if (m == "bx" && (o == "lr" || o == "r14")) return true;
            if (m == "pop" && o.find("pc") != std::string::npos) return true;
            if (m == "ldmfd" && o.find("pc") != std::string::npos) return true;
            if (m == "ldr" && o.find("pc") != std::string::npos) return true;
            return false;
        case Arch::PPC:
        case Arch::PPC64:
            // blr (branch to LR) is the canonical PPC return.
            if (m == "blr") return true;
            return false;
        case Arch::MIPS:
        case Arch::MIPS64:
            // jr $ra / jr $31 is the canonical MIPS return.
            if (m == "jr" && (o == "$ra" || o == "$31")) return true;
            return false;
        default:
            return false;
    }
}

std::size_t FunctionDetector::find_function_end(const std::vector<DecodedInstruction>& insns,
                                                 std::size_t start_idx) const {
    // Walk forward from start_idx until we hit a return instruction. The
    // return itself is included in the function body (callers slice
    // [start, end+1)). We also stop at the next prologue candidate to
    // avoid eating into the following function on mis-compiled binaries
    // without padding.
    for (std::size_t i = start_idx; i < insns.size(); ++i) {
        if (is_return(insns[i])) {
            return i + 1;  // include the return
        }
        // If we encounter another prologue before a return, stop here.
        if (i > start_idx) {
            const DecodedInstruction* second = (i + 1 < insns.size()) ? &insns[i + 1] : nullptr;
            bool is_prologue = false;
            switch (arch_) {
                case Arch::X86:
                case Arch::X86_64:  is_prologue = is_x86_prologue(insns[i], second); break;
                case Arch::AARCH64: is_prologue = is_arm64_prologue(insns[i], second); break;
                case Arch::ARM:     is_prologue = is_arm32_prologue(insns[i], second); break;
                case Arch::PPC:
                case Arch::PPC64:   is_prologue = is_ppc_prologue(insns[i], second); break;
                case Arch::MIPS:
                case Arch::MIPS64:  is_prologue = is_mips_prologue(insns[i], second); break;
                default: break;
            }
            if (is_prologue) return i;
        }
    }
    return insns.size();
}

}  // namespace nyx
