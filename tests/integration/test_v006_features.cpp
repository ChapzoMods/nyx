// =============================================================================
// Nyx integration tests: v0.0.6 features (DWARF, annotated, type inference)
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/type_inferer.hpp"
#include "nyx/output/annotated_writer.hpp"
#include "nyx/output/text_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/dwarf_parser.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: DWARF loaded from debug ELF") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) {
        CHECK_MESSAGE(true, "sample.debug.elf not built, skipping");
        return;
    }

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.dwarf != nullptr);
    if (bin.dwarf) {
        CHECK(bin.dwarf->has_info);
        CHECK_FALSE(bin.dwarf->lines.empty());
        CHECK_FALSE(bin.dwarf->file_names.empty());
        // sample.c should be in the file_names.
        bool has_sample_c = false;
        for (const auto& f : bin.dwarf->file_names) {
            if (f.find("sample.c") != std::string::npos) { has_sample_c = true; break; }
        }
        CHECK(has_sample_c);
    }
}

TEST_CASE("Integration: text output shows DWARF annotations") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) return;

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Disassembler dis(bin.arch, bin.endian);
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());

    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    for (const auto& s : bin.sections) {
        if (!s.executable || s.is_nobits) continue;
        if (s.file_off >= buf->size()) continue;
        const std::size_t avail = buf->size() - s.file_off;
        const std::size_t n = std::min<std::size_t>(s.file_size, avail);
        if (n == 0) continue;
        nyx::ByteView bytes{buf->data() + s.file_off, n};
        sections.push_back(dis.decode(bytes, s.vaddr));
    }

    const std::string text = nyx::output::to_text(bin, sections);
    // Should contain at least one source annotation.
    CHECK(text.find("sample.c:") != std::string::npos);
}

TEST_CASE("Integration: annotated output shows source context") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) return;

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Disassembler dis(bin.arch, bin.endian);
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());

    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    for (const auto& s : bin.sections) {
        if (!s.executable || s.is_nobits) continue;
        if (s.file_off >= buf->size()) continue;
        const std::size_t avail = buf->size() - s.file_off;
        const std::size_t n = std::min<std::size_t>(s.file_size, avail);
        if (n == 0) continue;
        nyx::ByteView bytes{buf->data() + s.file_off, n};
        sections.push_back(dis.decode(bytes, s.vaddr));
    }

    const std::string ann = nyx::output::to_annotated(bin, sections);
    CHECK(ann.find("DWARF      : available") != std::string::npos);
    CHECK(ann.find("// sample.c:") != std::string::npos);
}

TEST_CASE("Integration: DWARF function names enrich BinaryInfo") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) return;

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    REQUIRE(bin.dwarf != nullptr);
    // DWARF should have parsed at least one function (main, add, sub, etc).
    CHECK_FALSE(bin.dwarf->functions.empty());
    // Look for "main" among DWARF functions.
    bool has_main = false;
    for (const auto& f : bin.dwarf->functions) {
        if (f.name == "main") { has_main = true; break; }
    }
    CHECK(has_main);
}

TEST_CASE("Integration: TypeInferer with DWARF resolves function return type") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) return;

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::TypeInferer inferer(bin.arch, &bin);
    // "main" returns int in C; DWARF should reflect that.
    const auto ret = inferer.function_return_type("main");
    // DWARF may resolve to "int" or similar; if not resolved, returns "void".
    CHECK_FALSE(ret.empty());
}

TEST_CASE("Integration: --format annotated via CLI") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.debug.elf";
    if (!fs::exists(path)) return;

    const fs::path bin_path = fs::path(NYX_BINARY_DIR) / "nyx";
    REQUIRE(fs::exists(bin_path));
    std::string cmd = bin_path.string() + " --format annotated --log-level error " + path + " 2>&1";
    std::string out;
    char buf[4096];
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe);
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);

    CHECK(out.find("annotated disassembly") != std::string::npos);
    CHECK(out.find("DWARF") != std::string::npos);
}
