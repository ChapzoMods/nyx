// =============================================================================
// Nyx unit tests: lifter/ir.hpp
// =============================================================================
#include "nyx/lifter/ir.hpp"

#include <doctest/doctest.h>

TEST_CASE("Operand: factory builders") {
    auto r = nyx::ir::Operand::reg(7);
    CHECK(r.kind == nyx::ir::Operand::Kind::Register);
    CHECK(r.vreg == 7);

    auto i = nyx::ir::Operand::imm(0x1234);
    CHECK(i.kind == nyx::ir::Operand::Kind::Imm);
    CHECK(i.imm_value  == 0x1234);

    auto m = nyx::ir::Operand::mem(3, 16, 4);
    CHECK(m.kind == nyx::ir::Operand::Kind::Mem);
    CHECK(m.mem_base == 3);
    CHECK(m.mem_disp == 16);
    CHECK(m.mem_size == 4);

    auto l = nyx::ir::Operand::label(0x401000);
    CHECK(l.kind == nyx::ir::Operand::Kind::Label);
    CHECK(l.label_addr == 0x401000);

    auto s = nyx::ir::Operand::sym("printf");
    CHECK(s.kind == nyx::ir::Operand::Kind::Symbol);
    CHECK(s.symbol == "printf");
}

TEST_CASE("Builder: mov + binop sequence") {
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(10));
    b.mov(2, nyx::ir::Operand::imm(20));
    b.binop(nyx::ir::OpCode::Add, 3, nyx::ir::Operand::reg(1), nyx::ir::Operand::reg(2));

    auto insns = std::move(b).finish();
    REQUIRE(insns.size() == 3);
    CHECK(insns[0].op == nyx::ir::OpCode::Mov);
    CHECK(insns[1].op == nyx::ir::OpCode::Mov);
    CHECK(insns[2].op == nyx::ir::OpCode::Add);
    CHECK(insns[2].dst == 3);
    REQUIRE(insns[2].operands.size() == 2);
    CHECK(insns[2].operands[0].vreg == 1);
    CHECK(insns[2].operands[1].vreg == 2);
}

TEST_CASE("Instruction: is_terminator") {
    nyx::ir::Instruction i;
    i.op = nyx::ir::OpCode::Mov;
    CHECK_FALSE(i.is_terminator());
    i.op = nyx::ir::OpCode::Branch;     CHECK(i.is_terminator());
    i.op = nyx::ir::OpCode::BranchCond; CHECK(i.is_terminator());
    i.op = nyx::ir::OpCode::Return;     CHECK(i.is_terminator());
    i.op = nyx::ir::OpCode::Call;       CHECK_FALSE(i.is_terminator());
}

TEST_CASE("op_name: every opcode returns a non-empty string") {
    using namespace nyx::ir;
    for (auto op : {OpCode::Mov, OpCode::Load, OpCode::Store, OpCode::Add, OpCode::Sub,
                    OpCode::Mul, OpCode::Div, OpCode::Mod, OpCode::And, OpCode::Or,
                    OpCode::Xor, OpCode::Shl, OpCode::Shr, OpCode::Sar, OpCode::Neg,
                    OpCode::Not, OpCode::Cmp, OpCode::Branch, OpCode::BranchCond,
                    OpCode::Call, OpCode::Return, OpCode::Push, OpCode::Pop,
                    OpCode::Nop, OpCode::Opaque}) {
        CHECK_FALSE(op_name(op).empty());
    }
}
