// =============================================================================
// Nyx unit tests: MIPS lifter (v0.0.3)
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

TEST_CASE("MIPS lifter: addiu $sp, $sp, -0x10") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "addiu";
    d.op_str   = "$sp, $sp, -0x10";
    d.address  = 0x4001000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("MIPS lifter: sw $ra, 0x10($sp)") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "sw";
    d.op_str   = "$ra, 0x10($sp)";
    d.address  = 0x4001004;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Store);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Mem);
    CHECK(out.back().operands[0].mem_disp == 0x10);
}

TEST_CASE("MIPS lifter: lw $ra, 0x10($sp)") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "lw";
    d.op_str   = "$ra, 0x10($sp)";
    d.address  = 0x4001008;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Load);
}

TEST_CASE("MIPS lifter: jr $ra (return)") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "jr";
    d.op_str   = "$ra";
    d.address  = 0x400100c;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Return);
}

TEST_CASE("MIPS lifter: jal 0x4002000 (call)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jal";
    d.op_str   = "0x4002000";
    d.address  = 0x4001010;
    d.groups.push_back(2);
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
}

TEST_CASE("MIPS lifter: nop") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "nop";
    d.op_str   = "";
    d.address  = 0x4001014;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Nop);
}

TEST_CASE("MIPS lifter: addu $t0, $t1, $t2") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "addu";
    d.op_str   = "$t0, $t1, $t2";
    d.address  = 0x4001018;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("MIPS lifter: subu $t0, $t1, $t2") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "subu";
    d.op_str   = "$t0, $t1, $t2";
    d.address  = 0x400101c;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Sub);
}

TEST_CASE("MIPS lifter: and $t0, $t1, $t2") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "and";
    d.op_str   = "$t0, $t1, $t2";
    d.address  = 0x4001020;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::And);
}

TEST_CASE("MIPS lifter: lui $t0, 0x1000") {
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "lui";
    d.op_str   = "$t0, 0x1000";
    d.address  = 0x4001024;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
    // lui shifts left 16: 0x1000 << 16 = 0x10000000
    CHECK(out.back().operands[0].imm_value == 0x10000000);
}

TEST_CASE("MIPS lifter: beq $t0, $t1, 0x4001040 (conditional branch)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "beq";
    d.op_str   = "$t0, $t1, 0x4001040";
    d.address  = 0x4001028;
    d.groups.push_back(1);
    nyx::InstructionLifter lifter(nyx::Arch::MIPS, nyx::Endian::Big);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    // beq emits cmp + branch_cond.
    bool found_cond = false;
    for (const auto& i : out) if (i.op == nyx::ir::OpCode::BranchCond) found_cond = true;
    CHECK(found_cond);
}
