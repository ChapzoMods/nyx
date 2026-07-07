// =============================================================================
// Nyx unit tests: SSA optimizer (v0.2.0)
// =============================================================================
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"
#include "nyx/lifter/ssa_optimizer.hpp"

#include <doctest/doctest.h>

using namespace nyx::ir;

TEST_CASE("SSA opt: constant folding 2+3=5") {
    std::vector<Instruction> insns;
    Builder b;
    b.binop(OpCode::Add, 1, Operand::imm(2), Operand::imm(3));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto changed = constant_folding_pass(fn);
    CHECK(changed >= 1);
    // The add should now be a Mov of 5.
    CHECK(fn.blocks[0].instructions[0].op == OpCode::Mov);
    CHECK(fn.blocks[0].instructions[0].operands[0].imm_value == 5);
}

TEST_CASE("SSA opt: expression simplification v1+0 -> v1") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));
    b.binop(OpCode::Add, 2, Operand::reg(1), Operand::imm(0));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto changed = expression_simplification_pass(fn);
    CHECK(changed >= 1);
    // v2 = v1 + 0 should become v2 = v1 (Mov).
    CHECK(fn.blocks[0].instructions[1].op == OpCode::Mov);
}

TEST_CASE("SSA opt: v1*1 -> v1") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(10));
    b.binop(OpCode::Mul, 2, Operand::reg(1), Operand::imm(1));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto changed = expression_simplification_pass(fn);
    CHECK(changed >= 1);
    CHECK(fn.blocks[0].instructions[1].op == OpCode::Mov);
}

TEST_CASE("SSA opt: v1*0 -> 0") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(10));
    b.binop(OpCode::Mul, 2, Operand::reg(1), Operand::imm(0));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    [[maybe_unused]] auto changed = expression_simplification_pass(fn);
    CHECK(fn.blocks[0].instructions[1].op == OpCode::Mov);
    CHECK(fn.blocks[0].instructions[1].operands[0].imm_value == 0);
}

TEST_CASE("SSA opt: DCE removes unused vreg") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));  // v1 is never used
    b.mov(2, Operand::imm(10));  // v2 is used
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto removed = dead_code_elimination_pass(fn);
    CHECK(removed >= 1);
    // v1 should be removed; the block should have fewer instructions.
    CHECK(fn.blocks[0].instructions.size() < 3);
}

TEST_CASE("SSA opt: DCE keeps side-effect instructions") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));
    b.store(Operand::mem(INVALID_VREG, 0x1000), Operand::reg(1));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto removed = dead_code_elimination_pass(fn);
    CHECK(removed == 0);  // store uses v1, so v1 is kept
}

TEST_CASE("SSA opt: optimize() runs to fixpoint") {
    std::vector<Instruction> insns;
    Builder b;
    // v1 = 2 + 3 -> constant fold -> v1 = 5
    // v2 = v1 * 0 -> expr simplify -> v2 = 0
    // v2 is never used -> DCE removes it
    b.binop(OpCode::Add, 1, Operand::imm(2), Operand::imm(3));
    b.binop(OpCode::Mul, 2, Operand::reg(1), Operand::imm(0));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001; insns[2].addr = 0x1002;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");

    auto total = optimize(fn);
    CHECK(total > 0);
    // After optimization, v1 (2+3=5) should be folded to Mov(5),
    // v2 (v1*0=0) should be simplified to Mov(0), and since neither
    // is used, DCE should remove both. The function should have fewer
    // instructions than the original 3 (add, mul, ret).
    std::size_t total_insns = 0;
    for (const auto& blk : fn.blocks) {
        total_insns += blk.instructions.size();
    }
    CHECK(total_insns < 3);  // at least one instruction was removed
}
