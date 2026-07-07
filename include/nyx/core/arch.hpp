// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace nyx {

/// Architecture identifier. Order is fixed so it can be used as array index.
enum class Arch : std::uint8_t {
    Unknown = 0,
    X86,        // 32-bit Intel/AMD
    X86_64,     // 64-bit Intel/AMD
    ARM,        // 32-bit ARM (A32 / Thumb)
    AARCH64,    // 64-bit ARM
    PPC,        // 32-bit PowerPC
    PPC64,      // 64-bit PowerPC
    MIPS,       // 32-bit MIPS
    MIPS64,     // 64-bit MIPS
    RISCV,      // reserved for future versions
    Wasm,       // WebAssembly (virtual ISA, 32-bit addresses)
    __count,
};

/// Bitness of an architecture: 16, 32 or 64.
[[nodiscard]] std::uint8_t arch_bitness(Arch a) noexcept;

/// Canonical Capstone-style architecture name (lowercase, no spaces).
/// Examples: "x86", "x86-64", "arm", "arm64", "ppc", "ppc64", "mips", "mips64".
[[nodiscard]] std::string_view arch_name(Arch a) noexcept;

/// Pretty-printed name (e.g. "Intel x86-64").
[[nodiscard]] std::string_view arch_pretty(Arch a) noexcept;

/// Inverse of arch_name - case-insensitive.
[[nodiscard]] std::optional<Arch> arch_from_name(std::string_view s) noexcept;

/// Endianness of an architecture at the CPU level. Per-binary overrides
/// (e.g. bi-endian MIPS) are handled in the parser.
enum class Endian : std::uint8_t { Little, Big };
[[nodiscard]] Endian arch_default_endian(Arch a) noexcept;

/// Page size used for alignment heuristics (4 KiB everywhere for v0.0.1).
[[nodiscard]] constexpr std::uint64_t arch_page_size(Arch) noexcept { return 0x1000; }

/// Capstone mapping helpers - implemented in arch.cpp because the capstone
/// header is heavy and we don't want to leak it into every TU.
namespace capstone_map {
struct CsConfig {
    int cs_arch;     // CAPSTONE_ARCH_* value
    int cs_mode;     // CAPSTONE_MODE_* value
    bool little_endian;
};
[[nodiscard]] std::optional<CsConfig> for_arch(Arch a, Endian e);
}  // namespace capstone_map

}  // namespace nyx
