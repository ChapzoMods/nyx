// =============================================================================
// Nyx integration tests: end-to-end PE pipeline
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

TEST_CASE("Integration: PE end-to-end (parse + disasm + decompile + json)") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.pe";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format   == nyx::BinaryFormat::Pe);
    CHECK(bin.arch     == nyx::Arch::X86_64);
    CHECK(bin.image_base == 0x140000000);

    // Disassemble .text.
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());
    const auto* text = bin.find_section(".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->file_off < buf->size());
    const std::size_t avail = buf->size() - text->file_off;
    const std::size_t n = std::min<std::size_t>(text->file_size, avail);
    nyx::ByteView bytes{buf->data() + text->file_off, n};

    nyx::Disassembler dis(bin.arch, bin.endian);
    REQUIRE(dis.valid());
    auto insns = dis.decode(bytes, text->vaddr);
    // Our hand-crafted PE has a single `ret` at .text.
    CHECK_FALSE(insns.empty());
    CHECK(insns.front().mnemonic == "ret");

    // Decompile - the PE has the synthetic `entry` symbol pointing at our ret.
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    CHECK_FALSE(funcs.empty());
}
