// =============================================================================
// Nyx unit tests: dot output (v0.0.4)
// =============================================================================
#include "nyx/output/dot_writer.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"

#include <doctest/doctest.h>

TEST_CASE("dot writer: empty function produces valid digraph") {
    nyx::ir::Function fn;
    fn.name = "empty";
    fn.entry = 0x1000;
    const std::string s = nyx::output::to_dot(fn);
    CHECK(s.find("digraph") != std::string::npos);
    CHECK(s.find("rankdir=TB") != std::string::npos);
}

TEST_CASE("dot writer: single block with ret") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(42));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "test_fn");
    const std::string s = nyx::output::to_dot(fn);

    CHECK(s.find("digraph") != std::string::npos);
    CHECK(s.find("test_fn") != std::string::npos);
    CHECK(s.find("bb_1000_1000") != std::string::npos);  // node id
    CHECK(s.find("v1 =") != std::string::npos);           // instruction label
    CHECK(s.find("return;") != std::string::npos);
}

TEST_CASE("dot writer: if/else pattern produces labelled edges") {
    // block0: cmp + BranchCond(L_else)
    // block1: mov + Branch(L_end)
    // block2 (L_else): ret
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.cmp(1, nyx::ir::Operand::reg(2), nyx::ir::Operand::imm(0));
    b.branch_cond(nyx::ir::Operand::reg(1), 0x401020);
    b.mov(3, nyx::ir::Operand::imm(1));
    b.branch(0x401030);
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x401000;
    insns[1].addr = 0x401004;
    insns[2].addr = 0x401008;
    insns[3].addr = 0x40100c;
    insns[4].addr = 0x401020;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x401000, "if_else");
    const std::string s = nyx::output::to_dot(fn);

    CHECK(s.find("digraph") != std::string::npos);
    // The conditional edge should have a "cond" label.
    CHECK(s.find("cond") != std::string::npos);
    // There should be a fall-through edge.
    CHECK(s.find("fall-through") != std::string::npos);
    // Edges use -> syntax.
    CHECK(s.find("->") != std::string::npos);
}

TEST_CASE("dot writer: multiple functions wrapped in single digraph") {
    std::vector<nyx::ir::Function> fns;
    for (int i = 0; i < 2; ++i) {
        std::vector<nyx::ir::Instruction> insns;
        nyx::ir::Builder b;
        b.nop();
        b.ret();
        insns = std::move(b).finish();
        insns[0].addr = 0x1000 + i * 0x10;
        insns[1].addr = 0x1001 + i * 0x10;
        auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000 + i * 0x10,
                                              "fn" + std::to_string(i));
        fns.push_back(std::move(fn));
    }
    const std::string s = nyx::output::to_dot(fns);
    CHECK(s.find("digraph") != std::string::npos);
    CHECK(s.find("fn0") != std::string::npos);
    CHECK(s.find("fn1") != std::string::npos);
}

TEST_CASE("dot writer: escapes special characters in labels") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.opaque("foo \"bar\" \\ baz");
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "esc");
    const std::string s = nyx::output::to_dot(fn);
    // The double-quotes and backslash must be escaped.
    CHECK(s.find("\\\"bar\\\"") != std::string::npos);
    CHECK(s.find("\\\\ baz") != std::string::npos);
}
