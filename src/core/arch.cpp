// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/core/arch.hpp"

#include <capstone/capstone.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace nyx {

namespace {

struct ArchInfo {
    std::string_view name;
    std::string_view pretty;
    std::uint8_t     bitness;
    Endian           endian;
};

// Index by Arch enum value.
constexpr std::array<ArchInfo, static_cast<std::size_t>(Arch::__count)> kArchInfo = {{
    /* Unknown  */ {"unknown", "Unknown",         0,  Endian::Little},
    /* X86      */ {"x86",     "Intel x86 (32-bit)", 32, Endian::Little},
    /* X86_64   */ {"x86-64",  "Intel x86-64",       64, Endian::Little},
    /* ARM      */ {"arm",     "ARM (32-bit)",       32, Endian::Little},
    /* AARCH64  */ {"arm64",   "AArch64",            64, Endian::Little},
    /* PPC      */ {"ppc",     "PowerPC (32-bit)",   32, Endian::Big},
    /* PPC64    */ {"ppc64",   "PowerPC (64-bit)",   64, Endian::Big},
    /* MIPS     */ {"mips",    "MIPS (32-bit)",      32, Endian::Big},
    /* MIPS64   */ {"mips64",  "MIPS (64-bit)",      64, Endian::Big},
    /* RISCV    */ {"riscv",   "RISC-V",             0,  Endian::Little},
}};

}  // namespace

std::uint8_t arch_bitness(Arch a) noexcept {
    const auto i = static_cast<std::size_t>(a);
    if (i >= kArchInfo.size()) return 0;
    return kArchInfo[i].bitness;
}

std::string_view arch_name(Arch a) noexcept {
    const auto i = static_cast<std::size_t>(a);
    if (i >= kArchInfo.size()) return "unknown";
    return kArchInfo[i].name;
}

std::string_view arch_pretty(Arch a) noexcept {
    const auto i = static_cast<std::size_t>(a);
    if (i >= kArchInfo.size()) return "Unknown";
    return kArchInfo[i].pretty;
}

std::optional<Arch> arch_from_name(std::string_view s) noexcept {
    // Normalise to lowercase, trim spaces.
    std::string t;
    t.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '\t') continue;
        t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    for (std::size_t i = 0; i < kArchInfo.size(); ++i) {
        const auto& info = kArchInfo[i];
        if (info.name == t) return static_cast<Arch>(i);
    }

    // Common aliases.
    if (t == "amd64" || t == "x64" || t == "x86_64") return Arch::X86_64;
    if (t == "i386" || t == "i486" || t == "i586" || t == "i686") return Arch::X86;
    if (t == "aarch64" || t == "armv8") return Arch::AARCH64;
    if (t == "arm32" || t == "armv7")   return Arch::ARM;
    if (t == "powerpc")                 return Arch::PPC;
    if (t == "powerpc64" || t == "ppc64le") return Arch::PPC64;

    return std::nullopt;
}

Endian arch_default_endian(Arch a) noexcept {
    const auto i = static_cast<std::size_t>(a);
    if (i >= kArchInfo.size()) return Endian::Little;
    return kArchInfo[i].endian;
}

namespace capstone_map {

std::optional<CsConfig> for_arch(Arch a, Endian e) {
    CsConfig c{};
    c.little_endian = (e == Endian::Little);

    switch (a) {
        case Arch::X86:
            c.cs_arch = CS_ARCH_X86;
            c.cs_mode = CS_MODE_32;
            return c;
        case Arch::X86_64:
            c.cs_arch = CS_ARCH_X86;
            c.cs_mode = CS_MODE_64;
            return c;
        case Arch::ARM:
            c.cs_arch = CS_ARCH_ARM;
            c.cs_mode = CS_MODE_ARM;
            if (!c.little_endian) c.cs_mode |= CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::AARCH64:
            c.cs_arch = CS_ARCH_ARM64;
            c.cs_mode = CS_MODE_ARM;
            if (!c.little_endian) c.cs_mode |= CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::PPC:
            c.cs_arch = CS_ARCH_PPC;
            c.cs_mode = CS_MODE_32;
            if (!c.little_endian) c.cs_mode |= CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::PPC64:
            c.cs_arch = CS_ARCH_PPC;
            c.cs_mode = CS_MODE_64;
            if (!c.little_endian) c.cs_mode |= CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::MIPS:
            c.cs_arch = CS_ARCH_MIPS;
            c.cs_mode = CS_MODE_MIPS32;
            c.cs_mode |= c.little_endian ? CS_MODE_LITTLE_ENDIAN : CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::MIPS64:
            c.cs_arch = CS_ARCH_MIPS;
            c.cs_mode = CS_MODE_MIPS64;
            c.cs_mode |= c.little_endian ? CS_MODE_LITTLE_ENDIAN : CS_MODE_BIG_ENDIAN;
            return c;
        case Arch::Unknown:
        case Arch::RISCV:
        case Arch::__count:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace capstone_map

}  // namespace nyx
