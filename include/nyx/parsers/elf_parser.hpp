// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/parsers/binary_parser.hpp"

namespace nyx {

/// ELF parser - 32 and 64 bit, little and big endian. Pure C++20, no libelf.
/// Supports ELFOSABI_LINUX/ELFOSABI_SYSV/ELFOSABI_FREEBSD; other OSABI are
/// accepted but flagged via Logger::warn.
class ElfParser : public BinaryParser {
public:
    [[nodiscard]] BinaryFormat format() const noexcept override { return BinaryFormat::Elf; }
    [[nodiscard]] bool accepts(ByteView magic) const noexcept override;
    [[nodiscard]] BinaryInfo parse(ByteView data) const override;
};

}  // namespace nyx
