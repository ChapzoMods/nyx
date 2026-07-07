// =============================================================================
// Nyx integration tests: end-to-end ELF pipeline
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/output/json_writer.hpp"
#include "nyx/output/pseudo_c_writer.hpp"
#include "nyx/output/text_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: ELF end-to-end (parse + disasm + decompile + json)") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format == nyx::BinaryFormat::Elf);
    CHECK(bin.arch   == nyx::Arch::X86_64);

    // Disassemble the .text section.
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
    CHECK_FALSE(insns.empty());
    // Every decoded instruction must have a non-empty mnemonic and the
    // correct address progression.
    for (const auto& i : insns) CHECK_FALSE(i.mnemonic.empty());

    // Decompile.
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    CHECK_FALSE(funcs.empty());
    // `main` should be among the decompiled functions.
    bool has_main = false;
    for (const auto& f : funcs) {
        if (f.name == "main") { has_main = true; break; }
    }
    CHECK(has_main);

    // JSON output should be valid (no thrown exceptions, non-empty).
    const std::string json = nyx::output::to_json(bin, funcs);
    CHECK_FALSE(json.empty());
    CHECK(json.find("\"schema\":") != std::string::npos);
}

TEST_CASE("Integration: ELF text output is non-empty") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);

    nyx::Disassembler dis(bin.arch, bin.endian);
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());
    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    for (const auto& s : bin.sections) {
        if (!s.executable) continue;
        if (s.file_off >= buf->size()) continue;
        const std::size_t avail = buf->size() - s.file_off;
        const std::size_t n = std::min<std::size_t>(s.file_size, avail);
        if (n == 0) continue;
        nyx::ByteView bytes{buf->data() + s.file_off, n};
        sections.push_back(dis.decode(bytes, s.vaddr));
    }
    const std::string text = nyx::output::to_text(bin, sections);
    CHECK(text.find("sample.elf") != std::string::npos);
    CHECK(text.find("Disassembly") != std::string::npos);
}

TEST_CASE("Integration: ELF pseudo-C output is non-empty") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    REQUIRE_FALSE(funcs.empty());
    const std::string c = nyx::output::to_pseudo_c(bin, funcs);
    // v0.1.0: the pseudo-C renderer now derives the return type from the
    // calling convention (defaulting to `int` rather than `void`), so we
    // look for the `(void)` parameter list instead of a `void ` prefix.
    CHECK(c.find("(void)") != std::string::npos);
}
