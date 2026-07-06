// =============================================================================
// Nyx unit tests: text writer
// =============================================================================
#include "nyx/output/text_writer.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("Text writer: prints header and sections") {
    nyx::BinaryInfo bin;
    bin.format   = nyx::BinaryFormat::Elf;
    bin.arch     = nyx::Arch::X86_64;
    bin.endian   = nyx::Endian::Little;
    bin.is_64bit = true;
    bin.path     = "/tmp/sample.elf";

    nyx::Section s;
    s.name      = ".text";
    s.vaddr     = 0x401000;
    s.file_off  = 0x1000;
    s.file_size = 0x200;
    s.executable = true;
    s.is_code    = true;
    s.readable   = true;
    bin.sections.push_back(s);

    std::vector<std::vector<nyx::DecodedInstruction>> disasm;

    std::ostringstream os;
    nyx::output::write_text(os, bin, disasm);
    const std::string out = os.str();

    const bool has_nyx  = out.find("Nyx") != std::string::npos;
    const bool has_dump = out.find("Nyxdump") != std::string::npos;
    CHECK((has_nyx || has_dump));
    CHECK(out.find("/tmp/sample.elf") != std::string::npos);
    CHECK(out.find(".text") != std::string::npos);
    CHECK(out.find("x86-64") != std::string::npos);
}

TEST_CASE("Text writer: prints disassembly when present") {
    nyx::BinaryInfo bin;
    bin.format = nyx::BinaryFormat::Elf;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;
    bin.path   = "/tmp/sample.elf";

    nyx::DecodedInstruction d;
    d.address  = 0x401000;
    d.size     = 1;
    d.mnemonic = "ret";
    d.op_str   = "";
    d.bytes    = {0xc3};

    std::vector<std::vector<nyx::DecodedInstruction>> disasm = {{d}};
    std::ostringstream os;
    nyx::output::write_text(os, bin, disasm);
    const std::string out = os.str();

    CHECK(out.find("Disassembly") != std::string::npos);
    CHECK(out.find("ret") != std::string::npos);
    CHECK(out.find("401000") != std::string::npos);
}
