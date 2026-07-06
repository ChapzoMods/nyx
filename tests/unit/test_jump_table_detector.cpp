// =============================================================================
// Nyx unit tests: jump table detector + indirect branches (v0.0.5)
// =============================================================================
#include "nyx/decompiler/jump_table_detector.hpp"
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

TEST_CASE("JumpTableDetector: x86 indirect jmp with lea base") {
    // Simulate: lea rax, [rip + 0x1000]; jmp rax  (no table entries readable)
    std::vector<nyx::DecodedInstruction> insns(2);
    insns[0].mnemonic = "lea";
    insns[0].op_str   = "rax, [rip + 0x1000]";
    insns[0].address  = 0x401000;
    insns[0].size     = 7;
    insns[1].mnemonic = "jmp";
    insns[1].op_str   = "rax";
    insns[1].address  = 0x401007;

    nyx::JumpTableDetector det(nyx::Arch::X86_64, nyx::Endian::Little, nullptr);
    auto tables = det.detect(insns);
    // We expect at least one detection (table_base = 0x401000 + 7 + 0x1000).
    REQUIRE_FALSE(tables.empty());
    CHECK(tables[0].branch_addr == 0x401007);
    CHECK(tables[0].table_addr == 0x402007);  // 0x401000 + 7 + 0x1000
}

TEST_CASE("JumpTableDetector: direct jmp is not a jump table") {
    std::vector<nyx::DecodedInstruction> insns(1);
    insns[0].mnemonic = "jmp";
    insns[0].op_str   = "0x401020";
    insns[0].address  = 0x401000;

    nyx::JumpTableDetector det(nyx::Arch::X86_64, nyx::Endian::Little, nullptr);
    auto tables = det.detect(insns);
    CHECK(tables.empty());
}

TEST_CASE("Indirect branch: x86 jmp reg produces branch_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jmp";
    d.op_str   = "rax";
    d.address  = 0x401000;
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect call: x86 call reg produces call_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "call";
    d.op_str   = "rax";
    d.address  = 0x401000;
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect branch: ARM64 br xN produces branch_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "br";
    d.op_str   = "x0";
    d.address  = 0x1000;
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect call: ARM64 blr xN produces call_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "blr";
    d.op_str   = "x0";
    d.address  = 0x1000;
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect branch: MIPS jr $t0 produces branch_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jr";
    d.op_str   = "$t0";
    d.address  = 0x4001000;
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect call: MIPS jalr $t0 produces call_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jalr";
    d.op_str   = "$t0";
    d.address  = 0x4001000;
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
    CHECK(out.back().indirect == true);
}

TEST_CASE("Indirect branch: ARM32 bx r0 produces branch_indirect") {
    nyx::DecodedInstruction d;
    d.mnemonic = "bx";
    d.op_str   = "r0";
    d.address  = 0x8000;
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    CHECK(out.back().indirect == true);
}
