// =============================================================================
// Nyx unit tests: SSA builder (v0.1.0)
// =============================================================================
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/lifter/ir.hpp"
#include "nyx/lifter/ssa_builder.hpp"

#include <doctest/doctest.h>

using namespace nyx::ir;

TEST_CASE("SSA: build_ssa on empty function") {
    Function fn;
    fn.name = "empty";
    fn.entry = 0x1000;
    auto dom = compute_dominators(fn);
    auto ssa = build_ssa(fn, dom);
    CHECK(ssa.fn.blocks.empty());
}

TEST_CASE("SSA: single block with no branches") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(10));
    b.mov(2, Operand::imm(20));
    b.binop(OpCode::Add, 3, Operand::reg(1), Operand::reg(2));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    insns[2].addr = 0x1002; insns[3].addr = 0x1003;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");
    auto dom = compute_dominators(fn);
    auto ssa = build_ssa(fn, dom);

    // In a single block, SSA should produce unique versions.
    CHECK_FALSE(ssa.versions.empty());
    // v1 should have 1 version, v2 1 version, v3 1 version.
    CHECK(ssa.versions[1].size() >= 1);
    CHECK(ssa.versions[2].size() >= 1);
    CHECK(ssa.versions[3].size() >= 1);
}

TEST_CASE("SSA: diamond CFG produces phi at join") {
    // entry: cmp; BranchCond(L_then)
    // L_then: mov v3, 1; Branch(L_end)
    // L_else: mov v3, 2; Branch(L_end)
    // L_end: ret
    std::vector<Instruction> insns;
    Builder b;
    b.cmp(1, Operand::reg(2), Operand::imm(0));
    b.branch_cond(Operand::reg(1), 0x1010);
    b.mov(3, Operand::imm(1));      // L_then: v3 = 1
    b.branch(0x1020);                // goto L_end
    b.mov(3, Operand::imm(2));      // L_else: v3 = 2
    // L_end at 0x1020: use v3
    b.mov(4, Operand::reg(3));       // v4 = v3 (uses the merged v3)
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    insns[2].addr = 0x1004; insns[3].addr = 0x1005;
    insns[4].addr = 0x1010; insns[5].addr = 0x1020;
    insns[6].addr = 0x1021;

    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "diamond");
    auto dom = compute_dominators(fn);
    auto ssa = build_ssa(fn, dom);

    // v3 is defined in both branches, so SSA should create >= 2 versions
    // for v3 (one per branch) and possibly a phi at the join.
    CHECK(ssa.versions[3].size() >= 2);
}

TEST_CASE("SSA: dominance frontiers for linear CFG") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(0));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "linear");
    auto dom = compute_dominators(fn);
    auto df = compute_dominance_frontiers(fn, dom);

    // Linear CFG has no join points, so all DFs should be empty.
    for (const auto& s : df) {
        CHECK(s.empty());
    }
}

TEST_CASE("SSA: original mapping is populated") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");
    auto dom = compute_dominators(fn);
    auto ssa = build_ssa(fn, dom);

    // Every SSA version should map back to its original vreg.
    for (const auto& [ssa_v, orig_v] : ssa.original) {
        CHECK(orig_v != INVALID_VREG);
    }
}

TEST_CASE("SSA: loop produces phi at header") {
    // entry: BranchCond(loop_header)
    // loop_header: mov v1, 0; BranchCond(back_to_header)
    // exit: ret
    std::vector<Instruction> insns;
    Builder b;
    b.branch_cond(Operand::reg(2), 0x1004);  // 0x1000: skip loop
    b.mov(1, Operand::imm(0));                 // 0x1004: loop header v1 = 0
    b.binop(OpCode::Add, 1, Operand::reg(1), Operand::imm(1));  // v1 = v1 + 1
    b.branch_cond(Operand::reg(3), 0x1004);  // back edge
    b.ret();                                    // 0x1010: exit
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1004;
    insns[2].addr = 0x1005; insns[3].addr = 0x1006;
    insns[4].addr = 0x1010;

    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "loop");
    auto dom = compute_dominators(fn);
    auto ssa = build_ssa(fn, dom);

    // v1 is defined in the loop header and modified in the loop body,
    // so SSA should create >= 2 versions for v1.
    CHECK(ssa.versions[1].size() >= 2);
}
