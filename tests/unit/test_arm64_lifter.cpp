// =============================================================================
// Nyx unit tests: ARM64 lifter (v0.0.2)
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <doctest/doctest.h>

TEST_CASE("ARM64 lifter: mov x0, #0") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "x0, #0";
    d.address  = 0x100000000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
    CHECK(out.back().dst != nyx::ir::INVALID_VREG);
}

TEST_CASE("ARM64 lifter: add x2, x0, x1") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str   = "x2, x0, x1";
    d.address  = 0x100000008;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
    CHECK(out.back().operands.size() == 2);
}

TEST_CASE("ARM64 lifter: ldr x0, [x1, #0x10]") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "ldr";
    d.op_str   = "x0, [x1, #0x10]";
    d.address  = 0x100000010;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Load);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Mem);
    CHECK(out.back().operands[0].mem_disp == 0x10);
}

TEST_CASE("ARM64 lifter: str x0, [x1]") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "str";
    d.op_str   = "x0, [x1]";
    d.address  = 0x100000014;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Store);
}

TEST_CASE("ARM64 lifter: ret") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "ret";
    d.op_str   = "";
    d.address  = 0x100000018;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Return);
}

TEST_CASE("ARM64 lifter: b 0x100000020 (unconditional branch)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "b";
    d.op_str   = "0x100000020";
    d.address  = 0x100000000;
    d.groups.push_back(1);  // Jump
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    REQUIRE(out.back().operands.size() == 1);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Label);
    CHECK(out.back().operands[0].label_addr == 0x100000020);
}

TEST_CASE("ARM64 lifter: bl 0x100000040 (call)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "bl";
    d.op_str   = "0x100000040";
    d.address  = 0x100000000;
    d.groups.push_back(2);  // Call
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
}

TEST_CASE("ARM64 lifter: unknown mnemonic becomes Opaque") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "fjc";
    d.op_str   = "x0, x1";
    d.address  = 0x100000000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Opaque);
}

TEST_CASE("ARM64 lifter: sub x0, x1, x2") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "sub";
    d.op_str   = "x0, x1, x2";
    d.address  = 0x100000000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Sub);
}

TEST_CASE("ARM64 lifter: cmp x0, x1") {
    nyx::InstructionLifter lifter(nyx::Arch::AARCH64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "cmp";
    d.op_str   = "x0, x1";
    d.address  = 0x100000000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Cmp);
}
