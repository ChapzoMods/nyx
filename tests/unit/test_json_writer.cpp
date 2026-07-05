// =============================================================================
// Nyx unit tests: JSON writer
// =============================================================================
#include "nyx/output/json_writer.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("JSON writer: empty binary produces valid skeleton") {
    nyx::BinaryInfo bin;
    bin.format   = nyx::BinaryFormat::Elf;
    bin.arch     = nyx::Arch::X86_64;
    bin.endian   = nyx::Endian::Little;
    bin.is_64bit = true;
    bin.path     = "/dev/null";

    std::vector<nyx::DecompiledFunction> funcs;

    std::ostringstream os;
    nyx::output::write_json(os, bin, funcs);
    const std::string s = os.str();

    CHECK(s.find("\"schema\": \"nyx.v0.0.1\"") != std::string::npos);
    CHECK(s.find("\"format\": \"elf\"")         != std::string::npos);
    CHECK(s.find("\"arch\": \"x86-64\"")        != std::string::npos);
    // Empty functions array is emitted as `"functions": [\n  ]`.
    CHECK(s.find("\"functions\": [")            != std::string::npos);
    // Should end with a closing brace + newline.
    CHECK(s.back() == '\n');
}

TEST_CASE("JSON writer: escapes quotes and backslashes") {
    nyx::BinaryInfo bin;
    bin.path = "C:\\path with \"quotes\".bin";
    bin.format = nyx::BinaryFormat::Pe;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;

    std::ostringstream os;
    nyx::output::write_json(os, bin, {});
    const std::string s = os.str();

    CHECK(s.find("C:\\\\path with \\\"quotes\\\".bin") != std::string::npos);
}

TEST_CASE("JSON writer: includes function body lines") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86;
    bin.endian = nyx::Endian::Little;

    nyx::DecompiledFunction f;
    f.name        = "test_fn";
    f.entry       = 0x401000;
    f.block_count = 1;
    f.insn_count  = 2;
    f.lines       = {"void test_fn(void) {", "    return;", "}"};

    std::ostringstream os;
    nyx::output::write_json(os, bin, {f});
    const std::string s = os.str();

    CHECK(s.find("\"name\": \"test_fn\"")        != std::string::npos);
    CHECK(s.find("\"entry\": \"0x401000\"")      != std::string::npos);
    CHECK(s.find("void test_fn(void) {")         != std::string::npos);
}
