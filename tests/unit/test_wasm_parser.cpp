// =============================================================================
// Nyx unit tests: WASM parser (v0.2.0)
// =============================================================================
#include "nyx/parsers/wasm_parser.hpp"

#include <doctest/doctest.h>

TEST_CASE("WASM: accepts() magic bytes") {
    nyx::WasmParser p;
    const std::uint8_t good[] = {0x00, 0x61, 0x73, 0x6D};
    CHECK(p.accepts(nyx::ByteView{good, 4}));
    const std::uint8_t bad[] = {0x7F, 'E', 'L', 'F'};
    CHECK_FALSE(p.accepts(nyx::ByteView{bad, 4}));
}

TEST_CASE("WASM: parse_wasm on empty data returns nullopt") {
    auto r = nyx::parse_wasm(nyx::ByteView{});
    CHECK_FALSE(r.has_value());
}

TEST_CASE("WASM: parse_wasm on valid fixture") {
    // Minimal WASM: magic + version + type + func + export + code.
    // We build it inline to avoid file I/O in unit tests.
    const std::uint8_t wasm[] = {
        0x00, 0x61, 0x73, 0x6D,  // magic
        0x01, 0x00, 0x00, 0x00,  // version 1
        // Type section (id=1)
        0x01, 0x06,  // section id=1, size=6
        0x01,        // count=1
        0x60,        // func type
        0x01, 0x7F,  // 1 param: i32
        0x01, 0x7F,  // 1 result: i32
        // Function section (id=3)
        0x03, 0x02,  // section id=3, size=2
        0x01,        // count=1
        0x00,        // type index 0
        // Export section (id=7)
        0x07, 0x07,  // section id=7, size=7
        0x01,        // count=1
        0x03, 0x61, 0x64, 0x64,  // name="add"
        0x00,        // kind=func
        0x00,        // index=0
        // Code section (id=10)
        0x0A, 0x09,  // section id=10, size=9
        0x01,        // count=1
        0x07,        // body size=7
        0x00,        // 0 local declarations
        0x20, 0x00,  // local.get 0
        0x41, 0x01,  // i32.const 1
        0x6A,        // i32.add
        0x0B,        // end
    };

    auto info = nyx::parse_wasm(nyx::ByteView{wasm, sizeof(wasm)});
    REQUIRE(info.has_value());
    CHECK(info->version == 1);
    CHECK(info->types.size() == 1);
    CHECK(info->types[0].params.size() == 1);
    CHECK(info->types[0].params[0] == 0x7F);  // i32
    CHECK(info->func_type_indices.size() == 1);
    CHECK(info->func_type_indices[0] == 0);
    CHECK(info->exports.size() == 1);
    CHECK(info->exports[0].name == "add");
    CHECK(info->exports[0].kind == 0);  // func
    CHECK(info->exports[0].index == 0);
    CHECK(info->functions.size() == 1);
    CHECK(info->functions[0].code.size() == 6);  // local.get(2) + i32.const(2) + i32.add(1) + end(1)
}

TEST_CASE("WASM: WasmParser::parse populates BinaryInfo") {
    const std::uint8_t wasm[] = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7F, 0x01, 0x7F,
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00,
        0x0A, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x41, 0x01, 0x6A, 0x0B,
    };

    nyx::WasmParser p;
    auto bin = p.parse(nyx::ByteView{wasm, sizeof(wasm)});
    CHECK(bin.endian == nyx::Endian::Little);
    CHECK_FALSE(bin.symbols.empty());
    CHECK(bin.symbols[0].name == "add");
    CHECK(bin.symbols[0].kind == nyx::Symbol::Kind::Function);
    CHECK(bin.symbols[0].exported);
}

TEST_CASE("WASM: robustness - truncated binary doesn't crash") {
    const std::uint8_t trunc[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00};
    auto info = nyx::parse_wasm(nyx::ByteView{trunc, sizeof(trunc)});
    // Should return valid info (possibly empty functions/types) without crashing.
    if (info) {
        CHECK(info->functions.empty());
    }
}
