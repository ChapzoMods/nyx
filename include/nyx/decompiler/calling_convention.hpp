// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nyx {

/// v0.1.0: Calling convention identifier.
enum class CallingConvention : std::uint8_t {
    Unknown = 0,
    SysV_AMD64,      // System V AMD64 (Linux, macOS on x86-64)
    MS_x64,           // Microsoft x64 (Windows)
    AAPCS_ARM64,      // ARM Architecture Procedure Call Standard (AArch64)
    AAPCS_ARM32,      // AAPCS for ARM 32-bit
    MIPS_O32,         // MIPS O32 (32-bit, non-PIC)
    MIPS_N32,         // MIPS N32 (32-bit pointers, 64-bit regs)
    MIPS_N64,         // MIPS N64 (64-bit)
    PPC_SVR,          // PowerPC System V R4 (ELF)
    /// A simple "unknown but we have args" convention used when the
    /// architecture is recognised but the specific convention can't be
    /// determined. Uses the first N registers as arguments.
    Generic,
};

[[nodiscard]] std::string_view to_string(CallingConvention cc) noexcept;

/// Describes how arguments and return values are passed for a given
/// architecture + convention.
struct CallConventionInfo {
    CallingConvention conv = CallingConvention::Unknown;

    /// Register names used for integer/pointer arguments, in order.
    /// E.g. {"rdi", "rsi", "rdx", "rcx", "r8", "r9"} for SysV AMD64.
    std::vector<std::string> arg_regs;

    /// Register name for the return value (e.g. "rax", "x0", "$v0").
    std::string return_reg;

    /// Register names that are caller-saved (scratch).
    std::vector<std::string> caller_saved;

    /// Maximum number of integer arguments passed in registers before
    /// spilling to the stack.
    std::uint8_t max_reg_args = 0;
};

/// Returns the default calling convention info for the given architecture.
/// This is a best-effort heuristic: on Linux ELF binaries, x86-64 uses
/// SysV, ARM64 uses AAPCS, MIPS uses O32, PPC uses SVR.
[[nodiscard]] CallConventionInfo default_calling_convention(Arch arch) noexcept;

/// Maps a register name to a semantic parameter index. Returns nullopt
/// if the register is not an argument register for this convention.
[[nodiscard]] std::optional<std::uint8_t> register_to_param_index(
    const CallConventionInfo& cc, std::string_view reg_name) noexcept;

/// Returns a semantic name for a parameter index: "param1", "param2", etc.
[[nodiscard]] std::string param_name(std::uint8_t index) noexcept;

/// Returns "retval" for the return value register.
[[nodiscard]] constexpr std::string_view retval_name() noexcept { return "retval"; }

}  // namespace nyx
