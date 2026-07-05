// =============================================================================
// Nyx unit tests: pseudo-C writer
// =============================================================================
#include "nyx/output/pseudo_c_writer.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("Pseudo-C writer: header includes binary path") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/sample.elf";

    std::ostringstream os;
    nyx::output::write_pseudo_c(os, bin, {});
    const std::string out = os.str();

    CHECK(out.find("Nyx") != std::string::npos);
    CHECK(out.find("/tmp/sample.elf") != std::string::npos);
    CHECK(out.find("No functions were discovered") != std::string::npos);
}

TEST_CASE("Pseudo-C writer: emits function lines") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Pe;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "C:/sample.exe";

    nyx::DecompiledFunction f;
    f.name        = "add";
    f.entry       = 0x401000;
    f.block_count = 1;
    f.insn_count  = 1;
    f.lines       = {"void add(void) {", "    return;", "}"};

    std::ostringstream os;
    nyx::output::write_pseudo_c(os, bin, {f});
    const std::string out = os.str();

    CHECK(out.find("Function: add") != std::string::npos);
    CHECK(out.find("0x401000")      != std::string::npos);
    CHECK(out.find("void add(void) {") != std::string::npos);
}
