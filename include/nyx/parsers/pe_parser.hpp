// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/parsers/binary_parser.hpp"

namespace nyx {

/// PE/PE32+ parser. Pure C++20, no libpe. Reads COFF header, optional header
/// (PE32 / PE32+), section table, export directory (when present) and the
/// import descriptor table (limited). Handles both EXE and DLL.
class PeParser : public BinaryParser {
public:
    [[nodiscard]] BinaryFormat format() const noexcept override { return BinaryFormat::Pe; }
    [[nodiscard]] bool accepts(ByteView magic) const noexcept override;
    [[nodiscard]] BinaryInfo parse(ByteView data) const override;

private:
    void walk_exports(BinaryInfo& info, ByteView data,
                      std::uint32_t rva, std::uint32_t size,
                      std::uint64_t image_base) const;
    void walk_imports(BinaryInfo& info, ByteView data,
                      std::uint32_t rva, std::uint32_t size,
                      std::uint64_t image_base) const;
    [[nodiscard]] std::uint32_t rva_to_file_offset_fallback(ByteView data,
                                                            std::size_t sect_off,
                                                            std::uint16_t n_sections,
                                                            std::uint32_t rva,
                                                            std::uint64_t image_base) const;
};

}  // namespace nyx
