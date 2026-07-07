// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"
#include "nyx/parsers/binary_parser.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nyx {

/// v0.2.0: A parsed WASM function type signature.
struct WasmFuncType {
    std::vector<std::uint8_t> params;   // value types (0x7F=i32, 0x7E=i64, 0x7D=f32, 0x7C=f64)
    std::vector<std::uint8_t> results;
};

/// v0.2.0: A parsed WASM export entry.
struct WasmExport {
    std::string name;
    std::uint8_t kind = 0;   // 0=func, 1=table, 2=mem, 3=global
    std::uint32_t index = 0;
};

/// v0.2.0: A parsed WASM function body (code section entry).
struct WasmFuncBody {
    std::uint32_t func_idx = 0;
    std::vector<std::uint8_t> locals;  // flattened: repeated count * type
    ByteView code;                      // instruction bytes
};

/// v0.2.0: Result of parsing a WASM binary.
struct WasmInfo {
    std::uint32_t version = 0;
    std::vector<WasmFuncType> types;
    std::vector<std::uint32_t> func_type_indices;  // maps func idx -> type idx
    std::vector<WasmExport> exports;
    std::vector<WasmFuncBody> functions;
    bool has_info = false;
};

/// Parses a WASM binary from `data`. Returns nullopt on invalid magic.
[[nodiscard]] std::optional<WasmInfo> parse_wasm(ByteView data);

/// WASM parser class compatible with the BinaryParser interface.
class WasmParser : public BinaryParser {
public:
    [[nodiscard]] BinaryFormat format() const noexcept override { return BinaryFormat::Unknown; }
    [[nodiscard]] bool accepts(ByteView magic) const noexcept override;
    [[nodiscard]] BinaryInfo parse(ByteView data) const override;
};

}  // namespace nyx
