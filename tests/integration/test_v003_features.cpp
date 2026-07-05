// =============================================================================
// Nyx integration tests: v0.0.3 features (PPC/MIPS lifters, function detector)
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/function_detector.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: MIPS32 ELF parse + disasm + decompile") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.mips.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format   == nyx::BinaryFormat::Elf);
    CHECK(bin.arch     == nyx::Arch::MIPS);
    CHECK(bin.is_64bit == false);
    CHECK(bin.endian   == nyx::Endian::Big);

    // Disassemble .text.
    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());
    const auto* text = bin.find_section(".text");
    REQUIRE(text != nullptr);
    nyx::ByteView bytes{buf->data() + text->file_off, text->file_size};
    nyx::Disassembler dis(bin.arch, bin.endian);
    REQUIRE(dis.valid());
    auto insns = dis.decode(bytes, text->vaddr);
    REQUIRE(insns.size() == 7);
    CHECK(insns[0].mnemonic == "addiu");
    CHECK(insns[1].mnemonic == "sw");
    CHECK(insns[5].mnemonic == "jr");

    // Decompile - the FunctionDetector should find the prologue (addiu $sp).
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    CHECK_FALSE(funcs.empty());
}

TEST_CASE("Integration: PPC32 ELF parse + disasm + decompile") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.ppc.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    CHECK(bin.format   == nyx::BinaryFormat::Elf);
    CHECK(bin.arch     == nyx::Arch::PPC);
    CHECK(bin.is_64bit == false);
    CHECK(bin.endian   == nyx::Endian::Big);

    auto buf = nyx::ByteBuffer::from_file(path);
    REQUIRE(buf.has_value());
    const auto* text = bin.find_section(".text");
    REQUIRE(text != nullptr);
    nyx::ByteView bytes{buf->data() + text->file_off, text->file_size};
    nyx::Disassembler dis(bin.arch, bin.endian);
    REQUIRE(dis.valid());
    auto insns = dis.decode(bytes, text->vaddr);
    REQUIRE(insns.size() == 8);
    CHECK(insns[0].mnemonic == "stwu");
    CHECK(insns[7].mnemonic == "blr");

    // Decompile - FunctionDetector should find the stwu prologue.
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    CHECK_FALSE(funcs.empty());
}

TEST_CASE("Integration: FunctionDetector finds x86-64 prologues") {
    // Build a fake instruction stream: push rbp; mov rbp,rsp; ret; push rbp; ret
    std::vector<nyx::DecodedInstruction> insns(5);
    insns[0].mnemonic = "push"; insns[0].op_str = "rbp"; insns[0].address = 0x1000;
    insns[1].mnemonic = "mov";  insns[1].op_str = "rbp, rsp"; insns[1].address = 0x1001;
    insns[2].mnemonic = "ret";  insns[2].op_str = "";          insns[2].address = 0x1003;
    insns[3].mnemonic = "push"; insns[3].op_str = "rbp"; insns[3].address = 0x1004;
    insns[4].mnemonic = "ret";  insns[4].op_str = "";          insns[4].address = 0x1005;

    nyx::FunctionDetector det(nyx::Arch::X86_64);
    auto cands = det.detect(insns);
    CHECK(cands.size() == 2);
    CHECK(cands[0].address == 0x1000);
    CHECK(cands[1].address == 0x1004);
    CHECK(cands[0].name == "sub_1000");
}

TEST_CASE("Integration: FunctionDetector finds AArch64 prologues") {
    std::vector<nyx::DecodedInstruction> insns(2);
    insns[0].mnemonic = "stp"; insns[0].op_str = "x29, x30, [sp, #-16]!"; insns[0].address = 0x1000;
    insns[1].mnemonic = "ret"; insns[1].op_str = "";                       insns[1].address = 0x1004;

    nyx::FunctionDetector det(nyx::Arch::AARCH64);
    auto cands = det.detect(insns);
    CHECK(cands.size() == 1);
    CHECK(cands[0].address == 0x1000);
}

TEST_CASE("Integration: FunctionDetector finds MIPS prologues") {
    std::vector<nyx::DecodedInstruction> insns(2);
    insns[0].mnemonic = "addiu"; insns[0].op_str = "$sp, $sp, -0x10"; insns[0].address = 0x1000;
    insns[1].mnemonic = "nop";    insns[1].op_str = "";                 insns[1].address = 0x1004;

    nyx::FunctionDetector det(nyx::Arch::MIPS);
    auto cands = det.detect(insns);
    CHECK(cands.size() == 1);
    CHECK(cands[0].address == 0x1000);
}

TEST_CASE("Integration: FunctionDetector finds PPC prologues") {
    std::vector<nyx::DecodedInstruction> insns(2);
    insns[0].mnemonic = "stwu"; insns[0].op_str = "r1, -0x10(r1)"; insns[0].address = 0x1000;
    insns[1].mnemonic = "blr";   insns[1].op_str = "";              insns[1].address = 0x1004;

    nyx::FunctionDetector det(nyx::Arch::PPC);
    auto cands = det.detect(insns);
    CHECK(cands.size() == 1);
    CHECK(cands[0].address == 0x1000);
}

TEST_CASE("Integration: pseudo-C if/else hint for BranchCond+Branch pattern") {
    // Build a CFG manually: block0 = cmp + BranchCond(else), block1 = mov + Branch(end), block2 = ret
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.cmp(1, nyx::ir::Operand::reg(2), nyx::ir::Operand::imm(0));
    b.branch_cond(nyx::ir::Operand::reg(1), 0x401020);
    b.mov(3, nyx::ir::Operand::imm(1));
    b.branch(0x401030);
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x401000;
    insns[1].addr = 0x401004;
    insns[2].addr = 0x401008;
    insns[3].addr = 0x40100c;
    insns[4].addr = 0x401020;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x401000, "test_if_else");
    const std::string body = nyx::render_pseudo_c(fn);
    CHECK(body.find("if (") != std::string::npos);
    CHECK(body.find("goto") != std::string::npos);
}
