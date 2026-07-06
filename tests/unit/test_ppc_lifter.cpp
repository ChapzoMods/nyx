// =============================================================================
// Nyx unit tests: PowerPC lifter (v0.0.3)
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

TEST_CASE("PPC lifter: add r3, r4, r5") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str   = "r3, r4, r5";
    d.address  = 0x10001000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("PPC lifter: sub r3, r4, r5") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "sub";
    d.op_str   = "r3, r4, r5";
    d.address  = 0x10001004;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Sub);
}

TEST_CASE("PPC lifter: addi r3, r2, 0x10") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "addi";
    d.op_str   = "r3, r2, 0x10";
    d.address  = 0x10001008;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("PPC lifter: lwz r0, 0x14(r1)") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "lwz";
    d.op_str   = "r0, 0x14(r1)";
    d.address  = 0x1000100c;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Load);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Mem);
    CHECK(out.back().operands[0].mem_disp == 0x14);
}

TEST_CASE("PPC lifter: stw r0, 0x14(r1)") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "stw";
    d.op_str   = "r0, 0x14(r1)";
    d.address  = 0x10001010;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Store);
}

TEST_CASE("PPC lifter: mr r3, r4 (or alias)") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "mr";
    d.op_str   = "r3, r4";
    d.address  = 0x10001014;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
}

TEST_CASE("PPC lifter: blr (return)") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "blr";
    d.op_str   = "";
    d.address  = 0x10001018;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Return);
}

TEST_CASE("PPC lifter: bl 0x10002000 (call)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "bl";
    d.op_str   = "0x10002000";
    d.address  = 0x1000101c;
    d.groups.push_back(2);
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
}

TEST_CASE("PPC lifter: ori r3, r4, 0") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "ori";
    d.op_str   = "r3, r4, 0";
    d.address  = 0x10001020;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Or);
}

TEST_CASE("PPC lifter: nop") {
    nyx::InstructionLifter lifter(nyx::Arch::PPC, nyx::Endian::Big);
    nyx::DecodedInstruction d;
    d.mnemonic = "nop";
    d.op_str   = "";
    d.address  = 0x10001024;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Nop);
}
