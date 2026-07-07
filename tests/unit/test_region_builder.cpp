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
    CHECK(s.find("int f(") != std::string::npos);
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

TEST_CASE("Region: else-if chain nests IfThenElse in else slot") {
    // Build an else-if chain:
    //   A: if (c0) goto E (then0); else fall-through to B (else-if head)
    //   B: if (c1) goto D (then1); else fall-through to C (else2)
    //   C: else2; goto F (join)
    //   D: then1; goto F
    //   E: then0; goto F
    //   F: ret (join)
    //
    // The outer if at A is detected as IfThenElse because B's two
    // successors (D and C) converge at F, which equals E's successor.
    // The else slot (B) is then recursively nested as another IfThenElse.
    std::vector<Instruction> insns;
    Builder b;
    b.cmp(1, Operand::reg(2), Operand::imm(0));   // 0x1000 A
    b.branch_cond(Operand::reg(1), 0x1010);       // 0x1001 -> E (then0)
    b.cmp(4, Operand::reg(5), Operand::imm(0));   // 0x1004 B (else-if head)
    b.branch_cond(Operand::reg(4), 0x100C);       // 0x1005 -> D (then1)
    b.mov(7, Operand::imm(3));                     // 0x1008 C (else2)
    b.branch(0x1014);                              // 0x1009 -> F (join)
    b.mov(6, Operand::imm(2));                     // 0x100C D (then1)
    b.branch(0x1014);                              // 0x100D -> F
    b.mov(3, Operand::imm(1));                     // 0x1010 E (then0)
    b.branch(0x1014);                              // 0x1011 -> F
    b.ret();                                       // 0x1014 F (join)
    insns = std::move(b).finish();
    insns[0].addr  = 0x1000; insns[1].addr  = 0x1001;
    insns[2].addr  = 0x1004; insns[3].addr  = 0x1005;
    insns[4].addr  = 0x1008; insns[5].addr  = 0x1009;
    insns[6].addr  = 0x100C; insns[7].addr  = 0x100D;
    insns[8].addr  = 0x1010; insns[9].addr  = 0x1011;
    insns[10].addr = 0x1014;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "elseif");
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    auto root = structure_cfg(fn, dom, loops);

    // The root should contain an IfThenElse whose else slot is itself an
    // IfThenElse (the else-if chain).
    bool found_nested = false;
    for (const auto& c : root->children) {
        if (c->kind != Region::Kind::IfThenElse) continue;
        if (c->children.size() < 3) continue;
        if (c->children[2]->kind == Region::Kind::IfThenElse) {
            found_nested = true;
        }
    }
    CHECK(found_nested);
}
