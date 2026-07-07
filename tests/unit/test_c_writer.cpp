// =============================================================================
// Nyx unit tests: C output writer
// =============================================================================
#include "nyx/output/c_writer.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("C writer: header includes binary path and format") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/sample.elf";

    std::ostringstream os;
    nyx::output::write_c(os, bin, {});
    const std::string out = os.str();

    CHECK(out.find("Nyx") != std::string::npos);
    CHECK(out.find("/tmp/sample.elf") != std::string::npos);
    CHECK(out.find("elf") != std::string::npos);
    CHECK(out.find("x86-64") != std::string::npos);
    CHECK(out.find("No functions were discovered") != std::string::npos);
}

TEST_CASE("C writer: emits forward declarations and typedefs") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Pe;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "C:/sample.exe";

    nyx::DecompiledFunction f;
    f.name  = "add";
    f.entry = 0x401000;
    f.lines = {"void add(void) {", "  return;", "}"};

    std::ostringstream os;
    nyx::output::write_c(os, bin, {f});
    const std::string out = os.str();

    CHECK(out.find("typedef unsigned char u8;") != std::string::npos);
    CHECK(out.find("typedef unsigned short u16;") != std::string::npos);
    CHECK(out.find("typedef unsigned int u32;") != std::string::npos);
    CHECK(out.find("typedef unsigned long long u64;") != std::string::npos);
    CHECK(out.find("extern void call();") != std::string::npos);
    CHECK(out.find("void add(void);") != std::string::npos);
    CHECK(out.find("void add(void) {") != std::string::npos);
}

TEST_CASE("C writer: collects vregs into a global int block") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/x.elf";

    nyx::DecompiledFunction f;
    f.name = "f";
    f.entry = 0x1000;
    f.lines = {
        "void f(void) {",
        "  v1 = 0x2a;",
        "  v3 = v1 + 0x1;",
        "  return;",
        "}",
    };

    std::ostringstream os;
    nyx::output::write_c(os, bin, {f});
    const std::string out = os.str();

    CHECK(out.find("int v1, v3;") != std::string::npos);
}

TEST_CASE("C writer: to_c returns same content as write_c") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/x.elf";

    nyx::DecompiledFunction f;
    f.name = "g";
    f.entry = 0x2000;
    f.lines = {"void g(void) {", "  return;", "}"};

    std::ostringstream os;
    nyx::output::write_c(os, bin, {f});
    const std::string s = os.str();
    const std::string direct = nyx::output::to_c(bin, {f});
    CHECK(s == direct);
}
