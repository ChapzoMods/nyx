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
    // stmfd sp!, {fp, lr, ...}
    if (m == "stmfd" && o.find("lr") != std::string::npos) return true;
    return false;
}

bool FunctionDetector::is_ppc_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // stwu r1, -imm(r1)  - canonical PPC prologue
    if (m == "stwu" && o.find("r1") != std::string::npos && o.find("(r1)") != std::string::npos) return true;
    // mflr r0  - often precedes stwu
    if (m == "mflr" && (o == "r0" || o == "0")) return true;
    // stw r0, imm(r1)  - save LR
    if (m == "stw" && o.find("r0") != std::string::npos && o.find("(r1)") != std::string::npos) return true;
    return false;
}

bool FunctionDetector::is_mips_prologue(const DecodedInstruction& first, const DecodedInstruction* second) const {
    const std::string& m = first.mnemonic;
    const std::string& o = first.op_str;
    (void)second;

    // addiu sp, sp, -imm  (stack frame allocation; the negative immediate is the clue)
    if (m == "addiu" && o.find("$sp,") != std::string::npos) {
        // Look for a negative immediate (the '-' after the second comma).
        if (o.find(", -") != std::string::npos) return true;
    }
    // sw ra, imm(sp)  - save return address
    if (m == "sw" && o.find("$ra") != std::string::npos) return true;
    // sw fp, imm(sp)
    if (m == "sw" && o.find("$fp") != std::string::npos) return true;
    // daddiu sp, sp, -imm  (MIPS64)
    if (m == "daddiu" && o.find("$sp,") != std::string::npos && o.find(", -") != std::string::npos) return true;
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

}  // namespace nyx
