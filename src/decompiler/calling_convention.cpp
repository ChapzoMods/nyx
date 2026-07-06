// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/calling_convention.hpp"

#include <string>

namespace nyx {

std::string_view to_string(CallingConvention cc) noexcept {
    switch (cc) {
        case CallingConvention::Unknown:     return "unknown";
        case CallingConvention::SysV_AMD64:  return "sysv_amd64";
        case CallingConvention::MS_x64:       return "ms_x64";
        case CallingConvention::AAPCS_ARM64:  return "aapcs_arm64";
        case CallingConvention::AAPCS_ARM32:  return "aapcs_arm32";
        case CallingConvention::MIPS_O32:     return "mips_o32";
        case CallingConvention::MIPS_N32:     return "mips_n32";
        case CallingConvention::MIPS_N64:     return "mips_n64";
        case CallingConvention::PPC_SVR:      return "ppc_svr";
        case CallingConvention::Generic:      return "generic";
    }
    return "unknown";
}

CallConventionInfo default_calling_convention(Arch arch) noexcept {
    CallConventionInfo info;
    switch (arch) {
        case Arch::X86_64:
            info.conv = CallingConvention::SysV_AMD64;
            info.arg_regs = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
            info.return_reg = "rax";
            info.caller_saved = {"rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"};
            info.max_reg_args = 6;
            break;
        case Arch::X86:
            info.conv = CallingConvention::Generic;
            info.arg_regs = {};  // all stack-based on cdecl
            info.return_reg = "eax";
            info.max_reg_args = 0;
            break;
        case Arch::AARCH64:
            info.conv = CallingConvention::AAPCS_ARM64;
            info.arg_regs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
            info.return_reg = "x0";
            info.caller_saved = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
                                 "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18"};
            info.max_reg_args = 8;
            break;
        case Arch::ARM:
            info.conv = CallingConvention::AAPCS_ARM32;
            info.arg_regs = {"r0", "r1", "r2", "r3"};
            info.return_reg = "r0";
            info.caller_saved = {"r0", "r1", "r2", "r3", "r12"};
            info.max_reg_args = 4;
            break;
        case Arch::PPC:
        case Arch::PPC64:
            info.conv = CallingConvention::PPC_SVR;
            info.arg_regs = {"r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"};
            info.return_reg = "r3";
            info.max_reg_args = 8;
            break;
        case Arch::MIPS:
            info.conv = CallingConvention::MIPS_O32;
            info.arg_regs = {"$a0", "$a1", "$a2", "$a3"};
            info.return_reg = "$v0";
            info.max_reg_args = 4;
            break;
        case Arch::MIPS64:
            info.conv = CallingConvention::MIPS_N64;
            info.arg_regs = {"$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            info.return_reg = "$v0";
            info.max_reg_args = 8;
            break;
        default:
            info.conv = CallingConvention::Unknown;
            break;
    }
    return info;
}

std::optional<std::uint8_t> register_to_param_index(
    const CallConventionInfo& cc, std::string_view reg_name) noexcept {
    for (std::uint8_t i = 0; i < cc.arg_regs.size(); ++i) {
        if (cc.arg_regs[i] == reg_name) return i;
    }
    return std::nullopt;
}

std::string param_name(std::uint8_t index) noexcept {
    return "param" + std::to_string(index + 1);
}

}  // namespace nyx
