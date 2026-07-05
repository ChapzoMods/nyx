// =============================================================================
// Nyx unit tests: PE parser
// =============================================================================
#include "nyx/parsers/pe_parser.hpp"

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

TEST_CASE("PeParser: accepts() magic bytes") {
    nyx::PeParser p;
    const std::uint8_t mz[] = {'M', 'Z', 0, 0};
    CHECK(p.accepts(nyx::ByteView{mz, 4}));

    const std::uint8_t elf[] = {0x7f, 'E', 'L', 'F'};
    CHECK_FALSE(p.accepts(nyx::ByteView{elf, 4}));
}

TEST_CASE("PeParser: parse hand-crafted sample.pe") {
    auto buf = nyx::ByteBuffer::from_file(fixture("sample.pe"));
    REQUIRE(buf.has_value());

    nyx::PeParser p;
    REQUIRE(p.accepts(buf->view()));
    auto info = p.parse(buf->view());

    CHECK(info.format   == nyx::BinaryFormat::Pe);
    CHECK(info.arch     == nyx::Arch::X86_64);
    CHECK(info.is_64bit == true);
    CHECK(info.endian   == nyx::Endian::Little);
    CHECK(info.image_base == 0x140000000);
    CHECK(info.has_nx);

    // The PE should contain a .text section that's executable.
    const auto* text = info.find_section(".text");
    CHECK(text != nullptr);
    CHECK(text->executable);
    CHECK(text->is_code);

    // Synthetic entry symbol is always added.
    const auto* ep = info.find_symbol("entry");
    CHECK(ep != nullptr);
    CHECK(ep->kind == nyx::Symbol::Kind::Function);
}

TEST_CASE("PeParser: rejects garbage") {
    const std::uint8_t bogus[] = {'M', 'Z', 0, 0, 0, 0, 0, 0};
    nyx::PeParser p;
    bool threw = false;
    try { (void)p.parse(nyx::ByteView{bogus, 8}); } catch (const nyx::Error&) { threw = true; }
    CHECK(threw);
}
