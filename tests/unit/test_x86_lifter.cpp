// =============================================================================
// Nyx unit tests: x86/x86-64 lifter (v0.0.3)
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include <doctest/doctest.h>

TEST_CASE("x86-64 lifter: mov rax, 0x10") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "rax, 0x10";
    d.address  = 0x401000;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Mov);
    CHECK(out.back().dst != nyx::ir::INVALID_VREG);
}

TEST_CASE("x86-64 lifter: add rax, rbx") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str   = "rax, rbx";
    d.address  = 0x401003;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("x86-64 lifter: sub rsp, 0x20") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "sub";
    d.op_str   = "rsp, 0x20";
    d.address  = 0x401006;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Sub);
}

TEST_CASE("x86-64 lifter: xor rax, rax") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "xor";
    d.op_str   = "rax, rax";
    d.address  = 0x40100a;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Xor);
}

TEST_CASE("x86-64 lifter: cmp rax, 0x5") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "cmp";
    d.op_str   = "rax, 5";
    d.address  = 0x40100d;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Cmp);
}

TEST_CASE("x86-64 lifter: push rbp") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "push";
    d.op_str   = "rbp";
    d.address  = 0x401010;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Push);
}

TEST_CASE("x86-64 lifter: pop rbp") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "pop";
    d.op_str   = "rbp";
    d.address  = 0x401012;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Pop);
}

TEST_CASE("x86-64 lifter: ret") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "ret";
    d.op_str   = "";
    d.address  = 0x401014;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Return);
}

TEST_CASE("x86-64 lifter: lea rax, [rbp - 0x10]") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "lea";
    d.op_str   = "rax, [rbp - 0x10]";
    d.address  = 0x401016;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    // lea becomes a mov + optional add.
    bool found_mov = false;
    for (const auto& i : out) if (i.op == nyx::ir::OpCode::Mov) found_mov = true;
    CHECK(found_mov);
}

TEST_CASE("x86-64 lifter: inc rax") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "inc";
    d.op_str   = "rax";
    d.address  = 0x40101a;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Add);
}

TEST_CASE("x86-64 lifter: shl eax, 2") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "shl";
    d.op_str   = "eax, 2";
    d.address  = 0x40101c;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Shl);
}

TEST_CASE("x86-64 lifter: call 0x401020") {
    nyx::DecodedInstruction d;
    d.mnemonic = "call";
    d.op_str   = "0x401020";
    d.address  = 0x401000;
    d.groups.push_back(2);  // Call
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Call);
}

TEST_CASE("x86-64 lifter: jne 0x401030") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jne";
    d.op_str   = "0x401030";
    d.address  = 0x401000;
    d.groups.push_back(1);  // Jump
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::BranchCond);
}

TEST_CASE("x86-64 lifter: mov [rbp - 8], rdi (store)") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "[rbp - 8], rdi";
    d.address  = 0x401020;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    CHECK(out.back().op == nyx::ir::OpCode::Store);
}

TEST_CASE("x86-64 lifter: mov rax, [rbx + 0x10] (load)") {
    nyx::InstructionLifter lifter(nyx::Arch::X86_64, nyx::Endian::Little);
    nyx::DecodedInstruction d;
    d.mnemonic = "mov";
    d.op_str   = "rax, [rbx + 0x10]";
    d.address  = 0x401025;
    auto out = lifter.lift_one(d);
    REQUIRE_FALSE(out.empty());
    // mov reg, [mem] becomes a Load.
    bool found_load = false;
    for (const auto& i : out) if (i.op == nyx::ir::OpCode::Load) found_load = true;
    CHECK(found_load);
}
