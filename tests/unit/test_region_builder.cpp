// =============================================================================
// Nyx unit tests: Region builder (v0.2.0)
// =============================================================================
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/lifter/ir.hpp"
#include "nyx/lifter/region_builder.hpp"

#include <doctest/doctest.h>

using namespace nyx::ir;

TEST_CASE("Region: empty function produces empty sequence") {
    Function fn;
    fn.name = "empty";
    fn.entry = 0x1000;
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);
    CHECK(root->kind == Region::Kind::Sequence);
    CHECK(root->children.empty());
}

TEST_CASE("Region: single block becomes Block region") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);

    CHECK(root->kind == Region::Kind::Sequence);
    CHECK_FALSE(root->children.empty());
    CHECK(root->children[0]->kind == Region::Kind::Block);
}

TEST_CASE("Region: if-then pattern detected") {
    std::vector<Instruction> insns;
    Builder b;
    b.cmp(1, Operand::reg(2), Operand::imm(0));
    b.branch_cond(Operand::reg(1), 0x1008);
    b.mov(3, Operand::imm(1));  // then block
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    insns[2].addr = 0x1004; insns[3].addr = 0x1008;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "if_test");
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);

    // Should have at least one IfThen or IfThenElse region.
    bool found_if = false;
    for (const auto& c : root->children) {
        if (c->kind == Region::Kind::IfThen || c->kind == Region::Kind::IfThenElse) {
            found_if = true;
            break;
        }
    }
    CHECK(found_if);
}

TEST_CASE("Region: render_structured produces C output") {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(42));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "f");
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);

    std::string s = render_structured(fn, *root);
    CHECK(s.find("void f(void)") != std::string::npos);
    CHECK(s.find("return;") != std::string::npos);
}

TEST_CASE("Region: while loop detected") {
    // entry: BranchCond(loop_header)
    // loop_header: mov; BranchCond(back)
    // exit: ret
    std::vector<Instruction> insns;
    Builder b;
    b.branch_cond(Operand::reg(2), 0x1004);  // 0x1000
    b.mov(1, Operand::imm(0));                 // 0x1004 loop header
    b.branch_cond(Operand::reg(3), 0x1004);  // 0x1005 back edge
    b.ret();                                    // 0x1010
    insns = std::move(b).finish();
    insns[0].addr = 0x1000; insns[1].addr = 0x1004;
    insns[2].addr = 0x1005; insns[3].addr = 0x1010;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "loop");
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);

    bool found_loop = false;
    for (const auto& c : root->children) {
        if (c->kind == Region::Kind::While || c->kind == Region::Kind::DoWhile) {
            found_loop = true;
            break;
        }
    }
    CHECK(found_loop);
}
