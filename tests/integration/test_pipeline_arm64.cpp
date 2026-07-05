// =============================================================================
// Nyx integration tests: ARM64 Mach-O pipeline (v0.0.2)
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
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

TEST_CASE("Integration: ARM64 Mach-O parse + disasm + decompile") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format   == nyx::BinaryFormat::MachO);
    CHECK(bin.arch     == nyx::Arch::AARCH64);
    CHECK(bin.is_64bit == true);
    CHECK(bin.endian   == nyx::Endian::Little);

    // Disassemble __text.
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
    REQUIRE(insns.size() == 4);
    CHECK(insns[0].mnemonic == "mov");
    CHECK(insns[1].mnemonic == "mov");
    CHECK(insns[2].mnemonic == "add");
    CHECK(insns[3].mnemonic == "ret");

    // Decompile.
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    REQUIRE_FALSE(funcs.empty());
    const auto& fn = funcs.front();
    CHECK(fn.insn_count >= 4);

    // The pseudo-C body should reference vN variables (not be all Opaque).
    const std::string body = nyx::render_pseudo_c(
        nyx::InstructionLifter(nyx::Arch::AARCH64, nyx::Endian::Little)
            .lift_function(insns, "arm64_test"));
    CHECK(body.find("v1") != std::string::npos);
    CHECK(body.find("return;") != std::string::npos);
    // Should NOT be dominated by Opaque comments.
    CHECK(body.find("// mov") == std::string::npos);
}

TEST_CASE("Integration: ARM64 JSON output contains arch=arm64") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));
    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    const std::string json = nyx::output::to_json(bin, funcs);
    CHECK(json.find("\"arch\": \"arm64\"") != std::string::npos);
    CHECK(json.find("\"format\": \"mach-o\"") != std::string::npos);
}
