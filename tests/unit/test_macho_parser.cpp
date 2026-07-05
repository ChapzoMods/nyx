// =============================================================================
// Nyx unit tests: Mach-O parser
// =============================================================================
#include "nyx/parsers/macho_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"

#include <doctest/doctest.h>

#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace {
std::string fixture(const char* name) {
    return std::string(NYX_FIXTURES_DIR) + "/" + name;
}
}  // namespace

TEST_CASE("MachOParser: accepts() magics") {
    nyx::MachOParser p;
    const std::uint8_t mh_le[]   = {0xCE, 0xFA, 0xED, 0xFE};  // 0xFEEDFACE LE
    const std::uint8_t mh64_le[] = {0xCF, 0xFA, 0xED, 0xFE};  // 0xFEEDFACF LE
    const std::uint8_t mh_be[]   = {0xFE, 0xED, 0xFA, 0xCE};
    const std::uint8_t fat[]     = {0xCA, 0xFE, 0xBA, 0xBE};
    const std::uint8_t elf[]     = {0x7F, 'E', 'L', 'F'};

    CHECK(p.accepts(nyx::ByteView{mh_le,   4}));
    CHECK(p.accepts(nyx::ByteView{mh64_le, 4}));
    CHECK(p.accepts(nyx::ByteView{mh_be,   4}));
    CHECK(p.accepts(nyx::ByteView{fat,     4}));
    CHECK_FALSE(p.accepts(nyx::ByteView{elf, 4}));
}

TEST_CASE("MachOParser: parse hand-crafted sample.macho") {
    auto buf = nyx::ByteBuffer::from_file(fixture("sample.macho"));
    REQUIRE(buf.has_value());

    nyx::MachOParser p;
    REQUIRE(p.accepts(buf->view()));
    auto info = p.parse(buf->view());

    CHECK(info.format   == nyx::BinaryFormat::MachO);
    CHECK(info.arch     == nyx::Arch::X86_64);
    CHECK(info.is_64bit == true);
    CHECK(info.endian   == nyx::Endian::Little);

    // Hand-crafted fixture has a __text section inside __TEXT.
    const auto* text = info.find_section("__text");
    CHECK(text != nullptr);
    CHECK(text->executable);
    CHECK(text->is_code);

    // Entry point should be 0x100000000 (vmaddr of __TEXT, where our single
    // `ret` lives at offset 0xFFF in the file but maps to vmaddr + 0xFFF).
    CHECK(info.entry_point != 0);
}

TEST_CASE("MachOParser: rejects ELF magic") {
    const std::uint8_t elf[] = {0x7F, 'E', 'L', 'F'};
    nyx::MachOParser p;
    bool threw = false;
    try { (void)p.parse(nyx::ByteView{elf, 4}); } catch (const nyx::Error&) { threw = true; }
    CHECK(threw);
}
