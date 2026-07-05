// =============================================================================
// Nyx integration tests: end-to-end Mach-O pipeline
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/output/json_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: Mach-O end-to-end (parse + disasm + decompile)") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format   == nyx::BinaryFormat::MachO);
    CHECK(bin.arch     == nyx::Arch::X86_64);
    CHECK(bin.is_64bit == true);

    // Disassemble the __text section.
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());
    const auto* text = bin.find_section("__text");
    REQUIRE(text != nullptr);
    REQUIRE(text->file_off < buf->size());
    const std::size_t avail = buf->size() - text->file_off;
    const std::size_t n = std::min<std::size_t>(text->file_size, avail);
    nyx::ByteView bytes{buf->data() + text->file_off, n};

    nyx::Disassembler dis(bin.arch, bin.endian);
    REQUIRE(dis.valid());
    auto insns = dis.decode(bytes, text->vaddr);
    CHECK_FALSE(insns.empty());
    // First instruction should be `ret` (0xC3).
    CHECK(insns.front().mnemonic == "ret");

    // Linear-sweep fallback: Mach-O has no symbol table in our fixture, so
    // the decompiler should still produce one function via linear sweep.
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    CHECK_FALSE(funcs.empty());
}
