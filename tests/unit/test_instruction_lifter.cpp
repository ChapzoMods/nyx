// =============================================================================
// Nyx unit tests: InstructionLifter
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

#include <vector>

TEST_CASE("InstructionLifter: x86-64 ret produces a Return IR") {
    nyx::DecodedInstruction d;
    d.mnemonic = "ret";
    d.op_str   = "";
    d.address  = 0x401000;
    // Manually mark as return since we don't have Capstone groups in this fake.
    d.groups.push_back(2);  // 2 == InsnGroup::Ret internally

    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Return);
}

TEST_CASE("InstructionLifter: x86-64 mov reg, imm") {
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "rax, 0x10";
    d.address  = 0x401000;
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
    CHECK(out.back().dst != nyx::ir::INVALID_VREG);
}

TEST_CASE("InstructionLifter: x86-64 add reg, reg") {
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str   = "rax, rbx";
    d.address  = 0x401001;
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("InstructionLifter: x86-64 jmp imm becomes Branch") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jmp";
    d.op_str   = "0x401020";
    d.address  = 0x401000;
    d.groups.push_back(1);  // Jump
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Branch);
    REQUIRE(out.back().operands.size() == 1);
    CHECK(out.back().operands[0].kind == nyx::ir::Operand::Kind::Label);
    CHECK(out.back().operands[0].label_addr == 0x401020);
}

TEST_CASE("InstructionLifter: unknown mnemonic becomes Opaque") {
    nyx::DecodedInstruction d;
    d.mnemonic = "frobnicate";
    d.op_str   = "rax, rbx";
    d.address  = 0x401000;
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Opaque);
    CHECK(out.back().raw_mnemonic.find("frobnicate") != std::string::npos);
}
