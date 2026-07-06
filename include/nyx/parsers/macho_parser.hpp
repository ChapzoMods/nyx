// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/parsers/binary_parser.hpp"

namespace nyx {

/// Mach-O parser - 32 and 64 bit, little and big endian, fat/universal
/// archives included. Pure C++20 (no external Mach-O library).
/// Fat binaries: the first slice whose architecture Nyx recognises is the
/// one returned; remaining slices are logged via NYX_INFO.
class MachOParser : public BinaryParser {
public:
    [[nodiscard]] BinaryFormat format() const noexcept override { return BinaryFormat::MachO; }
    [[nodiscard]] bool accepts(ByteView magic) const noexcept override;
    [[nodiscard]] BinaryInfo parse(ByteView data) const override;
};

}  // namespace nyx
