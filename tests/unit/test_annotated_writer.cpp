// =============================================================================
// Nyx unit tests: annotated writer (v0.0.6)
// =============================================================================
#include "nyx/output/annotated_writer.hpp"
#include "nyx/parsers/dwarf_parser.hpp"

#include <doctest/doctest.h>

#include <sstream>

TEST_CASE("Annotated writer: no DWARF produces plain disassembly") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/test.elf";

    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    nyx::DecodedInstruction d;
    d.address  = 0x1000;
    d.size     = 1;
    d.mnemonic = "ret";
    d.op_str   = "";
    d.bytes    = {0xc3};
    sections.push_back({d});

    std::ostringstream os;
    nyx::output::write_annotated(os, bin, sections);
    const std::string out = os.str();

    CHECK(out.find("annotated disassembly") != std::string::npos);
    CHECK(out.find("DWARF      : not available") != std::string::npos);
    CHECK(out.find("ret") != std::string::npos);
}

TEST_CASE("Annotated writer: with DWARF shows source context") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/test.elf";
    bin.dwarf = std::make_shared<nyx::DwarfInfo>();
    bin.dwarf->has_info = true;
    bin.dwarf->file_names = {"main.c"};
    bin.dwarf->lines.push_back({0x1000, 1, 42, 0, true, false});
    bin.dwarf->lines.push_back({0x1004, 1, 43, 0, true, false});

    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    nyx::DecodedInstruction d1;
    d1.address = 0x1000; d1.size = 1; d1.mnemonic = "push"; d1.op_str = "rbp";
    d1.bytes = {0x55};
    nyx::DecodedInstruction d2;
    d2.address = 0x1004; d2.size = 1; d2.mnemonic = "ret"; d2.op_str = "";
    d2.bytes = {0xc3};
    sections.push_back({d1, d2});

    std::ostringstream os;
    nyx::output::write_annotated(os, bin, sections);
    const std::string out = os.str();

    CHECK(out.find("DWARF      : available") != std::string::npos);
    CHECK(out.find("main.c:42") != std::string::npos);
    CHECK(out.find("main.c:43") != std::string::npos);
    CHECK(out.find("push") != std::string::npos);
}

TEST_CASE("Annotated writer: empty sections produce header only") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/empty.elf";

    std::vector<std::vector<nyx::DecodedInstruction>> sections;
    std::ostringstream os;
    nyx::output::write_annotated(os, bin, sections);
    const std::string out = os.str();

    CHECK(out.find("annotated disassembly") != std::string::npos);
    CHECK(out.find("DWARF      : not available") != std::string::npos);
}
