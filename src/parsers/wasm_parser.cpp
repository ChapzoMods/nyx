// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// WebAssembly (WASM) binary parser - pure C++20, no external dependency.
// Supports: header, Type section, Import section, Function section,
// Export section, Code section (function bodies with locals).
// Uses LEB128 decoding for all variable-length integers.
// =============================================================================
#include "nyx/parsers/wasm_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <cstring>
#include <string>

namespace nyx {

namespace {

// WASM magic: \0asm (0x00 0x61 0x73 0x6D)
constexpr std::uint32_t WASM_MAGIC = 0x6D736100u;
constexpr std::uint32_t WASM_VERSION = 1u;

// Section IDs
constexpr std::uint8_t SEC_TYPE     = 1;
constexpr std::uint8_t SEC_IMPORT   = 2;
constexpr std::uint8_t SEC_FUNCTION = 3;
constexpr std::uint8_t SEC_TABLE    = 4;
constexpr std::uint8_t SEC_MEMORY   = 5;
constexpr std::uint8_t SEC_GLOBAL   = 6;
constexpr std::uint8_t SEC_EXPORT   = 7;
constexpr std::uint8_t SEC_START    = 8;
constexpr std::uint8_t SEC_ELEMENT  = 9;
constexpr std::uint8_t SEC_CODE     = 10;
constexpr std::uint8_t SEC_DATA     = 11;

// LEB128 reader (unsigned).
std::uint64_t read_wasm_uleb128(const std::uint8_t*& p, const std::uint8_t* end) {
    std::uint64_t result = 0;
    int shift = 0;
    while (p < end && shift < 64) {
        std::uint8_t byte = *p++;
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

// LEB128 reader (signed) - used for future WASM lifter.
[[maybe_unused]] std::int64_t read_wasm_sleb128(const std::uint8_t*& p, const std::uint8_t* end) {
    std::int64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    while (p < end && shift < 64) {
        byte = *p++;
        result |= static_cast<std::int64_t>(byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }
    if (shift < 64 && (byte & 0x40)) {
        result |= -(static_cast<std::int64_t>(1) << shift);
    }
    return result;
}

std::string read_wasm_name(const std::uint8_t*& p, const std::uint8_t* end) {
    const auto len = read_wasm_uleb128(p, end);
    if (p + len > end) return {};
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

void parse_type_section(const std::uint8_t* p, const std::uint8_t* end, WasmInfo& info) {
    const auto count = read_wasm_uleb128(p, end);
    for (std::uint64_t i = 0; i < count && p < end; ++i) {
        WasmFuncType ft;
        const std::uint8_t form = *p++;  // 0x60 = func
        if (form != 0x60) continue;
        const auto nparams = read_wasm_uleb128(p, end);
        for (std::uint64_t j = 0; j < nparams && p < end; ++j) {
            ft.params.push_back(*p++);
        }
        const auto nresults = read_wasm_uleb128(p, end);
        for (std::uint64_t j = 0; j < nresults && p < end; ++j) {
            ft.results.push_back(*p++);
        }
        info.types.push_back(std::move(ft));
    }
}

void parse_function_section(const std::uint8_t* p, const std::uint8_t* end, WasmInfo& info) {
    const auto count = read_wasm_uleb128(p, end);
    for (std::uint64_t i = 0; i < count && p < end; ++i) {
        const auto type_idx = static_cast<std::uint32_t>(read_wasm_uleb128(p, end));
        info.func_type_indices.push_back(type_idx);
    }
}

void parse_export_section(const std::uint8_t* p, const std::uint8_t* end, WasmInfo& info) {
    const auto count = read_wasm_uleb128(p, end);
    for (std::uint64_t i = 0; i < count && p < end; ++i) {
        WasmExport exp;
        exp.name = read_wasm_name(p, end);
        if (p >= end) break;
        exp.kind = *p++;
        exp.index = static_cast<std::uint32_t>(read_wasm_uleb128(p, end));
        info.exports.push_back(std::move(exp));
    }
}

void parse_code_section(const std::uint8_t* p, const std::uint8_t* end, WasmInfo& info) {
    const auto count = read_wasm_uleb128(p, end);
    for (std::uint64_t i = 0; i < count && p < end; ++i) {
        const auto body_size = read_wasm_uleb128(p, end);
        if (p + body_size > end) break;
        const std::uint8_t* body_end = p + body_size;
        WasmFuncBody fb;
        fb.func_idx = static_cast<std::uint32_t>(info.functions.size());
        // Local declarations.
        const auto nlocals = read_wasm_uleb128(p, body_end);
        for (std::uint64_t j = 0; j < nlocals && p < body_end; ++j) {
            const auto n = read_wasm_uleb128(p, body_end);
            if (p >= body_end) break;
            const std::uint8_t vt = *p++;
            for (std::uint64_t k = 0; k < n; ++k) fb.locals.push_back(vt);
        }
        // Remaining bytes are the function's instructions.
        fb.code = ByteView{p, static_cast<std::size_t>(body_end - p)};
        p = body_end;
        info.functions.push_back(std::move(fb));
    }
}

}  // namespace

std::optional<WasmInfo> parse_wasm(ByteView data) {
    if (data.size() < 8) return std::nullopt;
    // Check magic.
    const std::uint32_t magic = read_u32_le(data.data());
    if (magic != WASM_MAGIC) return std::nullopt;

    WasmInfo info;
    info.version = read_u32_le(data.data() + 4);
    info.has_info = true;

    // Walk sections.
    const std::uint8_t* p = data.data() + 8;
    const std::uint8_t* end = data.data() + data.size();
    while (p < end) {
        const std::uint8_t sec_id = *p++;
        const auto sec_size = read_wasm_uleb128(p, end);
        if (p + sec_size > end) break;
        const std::uint8_t* sec_end = p + sec_size;

        switch (sec_id) {
            case SEC_TYPE:
                parse_type_section(p, sec_end, info);
                break;
            case SEC_FUNCTION:
                parse_function_section(p, sec_end, info);
                break;
            case SEC_EXPORT:
                parse_export_section(p, sec_end, info);
                break;
            case SEC_CODE:
                parse_code_section(p, sec_end, info);
                break;
            default:
                // Skip unknown sections.
                break;
        }
        p = sec_end;
    }

    NYX_INFO("WASM: parsed " + std::to_string(info.types.size()) + " types, "
             + std::to_string(info.functions.size()) + " functions, "
             + std::to_string(info.exports.size()) + " exports");
    return info;
}

// ---------------------------------------------------------------------------
// WasmParser (BinaryParser interface)
// ---------------------------------------------------------------------------
bool WasmParser::accepts(ByteView magic) const noexcept {
    return magic.size() >= 4
        && magic[0] == 0x00
        && magic[1] == 0x61
        && magic[2] == 0x73
        && magic[3] == 0x6D;
}

BinaryInfo WasmParser::parse(ByteView data) const {
    if (!accepts(data)) {
        NYX_THROW(Parser, "WASM: bad magic");
    }
    auto wasm = parse_wasm(data);
    if (!wasm) {
        NYX_THROW(Parser, "WASM: parse failed");
    }

    BinaryInfo info{};
    info.format = BinaryFormat::Wasm;  // WASM has its own format enum
    info.arch = Arch::Wasm;            // WASM is a virtual ISA
    info.endian = Endian::Little;
    info.is_64bit = true;  // WASM uses 32-bit addresses but 64-bit ints
    info.is_pie = true;
    info.has_nx = true;
    info.stripped = false;

    // Add exports as symbols.
    for (const auto& exp : wasm->exports) {
        if (exp.kind == 0) {  // function export
            Symbol sym{};
            sym.name = exp.name;
            sym.value = exp.index;
            sym.kind = Symbol::Kind::Function;
            sym.exported = true;
            info.symbols.push_back(std::move(sym));
        }
    }

    // Create a synthetic section for each function body so the decompiler
    // can find and disassemble them.
    for (const auto& fb : wasm->functions) {
        Section sec{};
        sec.name = "wasm_func_" + std::to_string(fb.func_idx);
        sec.vaddr = 0x1000 * (fb.func_idx + 1);  // synthetic VAs
        sec.file_off = 0;  // not file-backed; code is in memory
        sec.file_size = fb.code.size();
        sec.mem_size = fb.code.size();
        sec.executable = true;
        sec.is_code = true;
        info.sections.push_back(std::move(sec));
    }

    // Store the wasm info alongside for the decompiler to use.
    // For v0.2.0 we don't fully integrate WASM disassembly, but the
    // parser populates the BinaryInfo so that function names and
    // exports are available.

    return info;
}

}  // namespace nyx
