// =============================================================================
// Nyx unit tests: ELF parser
// =============================================================================
#include "nyx/parsers/elf_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace {
std::string fixture(const char* name) {
    return std::string(NYX_FIXTURES_DIR) + "/" + name;
}
}  // namespace

TEST_CASE("ElfParser: accepts() magic bytes") {
    nyx::ElfParser p;
    const std::uint8_t good[] = {0x7f, 'E', 'L', 'F'};
    CHECK(p.accepts(nyx::ByteView{good, 4}));

    const std::uint8_t bad[]  = {'M', 'Z', 0, 0};
    CHECK_FALSE(p.accepts(nyx::ByteView{bad, 4}));

    CHECK_FALSE(p.accepts(nyx::ByteView{}));
}

TEST_CASE("ElfParser: parse sample.elf (x86-64)") {
    auto buf = nyx::ByteBuffer::from_file(fixture("sample.elf"));
    REQUIRE(buf.has_value());

    nyx::ElfParser p;
    REQUIRE(p.accepts(buf->view()));
    auto info = p.parse(buf->view());

    CHECK(info.format   == nyx::BinaryFormat::Elf);
    CHECK(info.arch     == nyx::Arch::X86_64);
    CHECK(info.is_64bit == true);
    CHECK(info.endian   == nyx::Endian::Little);
    CHECK(info.entry_point != 0);

    // Should have at least .text section marked executable.
    const auto* text = info.find_section(".text");
    CHECK(text != nullptr);
    CHECK(text->executable);
    CHECK(text->file_size > 0);

    // sample.c has `main`, `add`, `sub`, `use_add_sub` symbols.
    const auto* main_sym = info.find_symbol("main");
    CHECK(main_sym != nullptr);
    CHECK(main_sym->kind == nyx::Symbol::Kind::Function);
}

TEST_CASE("ElfParser: rejects truncated ELF") {
    const std::uint8_t truncated[] = {0x7f, 'E', 'L', 'F', 1, 1};
    nyx::ElfParser p;
    // 6 bytes is too short for a real header; parse() must throw.
    bool threw = false;
    try { (void)p.parse(nyx::ByteView{truncated, 6}); } catch (const nyx::Error&) { threw = true; }
    CHECK(threw);
}
