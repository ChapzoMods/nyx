// =============================================================================
// Nyx unit tests: lifter/cfg.hpp
// =============================================================================
#include "nyx/lifter/cfg.hpp"

#include <doctest/doctest.h>

TEST_CASE("CFGBuilder: empty input") {
    auto fn = nyx::ir::CFGBuilder::build({}, 0, "empty");
    CHECK(fn.blocks.empty());
    CHECK(fn.name == "empty");
}

TEST_CASE("CFGBuilder: single block with ret") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(0));
    b.ret();
    insns = std::move(b).finish();
    for (auto& i : insns) i.addr = 0x1000 + i.addr;  // dummy addresses
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    REQUIRE(fn.blocks.size() == 1);
    CHECK(fn.blocks[0].instructions.size() == 2);
    CHECK(fn.blocks[0].instructions.back().op == nyx::ir::OpCode::Return);
    CHECK(fn.entry == 0x1000);
}

TEST_CASE("CFGBuilder: split on branch target") {
    // Block 0: 0x1000 mov; 0x1001 br 0x1003
    // Block 1: 0x1002 nop  (fall-through, but unreachable from block 0)
    // Block 2: 0x1003 mov; 0x1004 ret
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(1));
    b.branch(0x1003);
    b.nop();
    b.mov(2, nyx::ir::Operand::imm(2));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    insns[2].addr = 0x1002;
    insns[3].addr = 0x1003;
    insns[4].addr = 0x1004;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    CHECK(fn.blocks.size() >= 2);
    CHECK(fn.instruction_count() == 5);
    CHECK(fn.block_count() == fn.blocks.size());
}

TEST_CASE("Function: ensure_block idempotent") {
    nyx::ir::Function fn;
    auto i1 = fn.ensure_block(0x1000);
    auto i2 = fn.ensure_block(0x1000);
    CHECK(i1 == i2);
    CHECK(fn.blocks.size() == 1);
    CHECK(fn.blocks[i1].start_addr == 0x1000);
}
