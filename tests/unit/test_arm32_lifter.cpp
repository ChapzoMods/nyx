// =============================================================================
// Nyx unit tests: ARM32 lifter (v0.0.3)
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

TEST_CASE("ARM32 lifter: mov r0, r1") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "r0, r1";
    d.address  = 0x8000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
}

TEST_CASE("ARM32 lifter: add r0, r1, r2") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str   = "r0, r1, r2";
    d.address  = 0x8004;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("ARM32 lifter: sub r0, r1, r2") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "sub";
    d.op_str   = "r0, r1, r2";
    d.address  = 0x8008;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Sub);
}

TEST_CASE("ARM32 lifter: ldr r0, [r1, #0x4]") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "ldr";
    d.op_str   = "r0, [r1, #0x4]";
    d.address  = 0x800c;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Load);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Mem);
    CHECK(out.back().operands[0].mem_disp == 4);
}

TEST_CASE("ARM32 lifter: str r0, [r1]") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "str";
    d.op_str   = "r0, [r1]";
    d.address  = 0x8010;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Store);
}

TEST_CASE("ARM32 lifter: cmp r0, r1") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "cmp";
    d.op_str   = "r0, r1";
    d.address  = 0x8014;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Cmp);
}

TEST_CASE("ARM32 lifter: b 0x8050 (unconditional branch)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "b";
    d.op_str   = "0x8050";
    d.address  = 0x8018;
    d.groups.push_back(1);
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    CHECK(out.back().operands[0].label_addr == 0x8050);
}

TEST_CASE("ARM32 lifter: bl 0x8060 (call)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "bl";
    d.op_str   = "0x8060";
    d.address  = 0x801c;
    d.groups.push_back(2);
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
}

TEST_CASE("ARM32 lifter: bx lr (return)") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "bx";
    d.op_str   = "lr";
    d.address  = 0x8020;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Opaque);
}

TEST_CASE("ARM32 lifter: and r0, r1, r2") {
    nyx::InstructionLifter lifter(nyx::Arch::ARM, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "and";
    d.op_str   = "r0, r1, r2";
    d.address  = 0x8024;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::And);
}
