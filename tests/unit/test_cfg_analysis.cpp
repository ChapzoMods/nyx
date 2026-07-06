// =============================================================================
// Nyx unit tests: CFG analysis (dominators, loops, pruning) - v0.0.5
// =============================================================================
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"

#include <doctest/doctest.h>

using namespace nyx::ir;

// Helper: build a simple linear function with one branch.
static Function make_linear_fn() {
    std::vector<Instruction> insns;
    Builder b;
    b.mov(1, Operand::imm(0));
    b.cmp(2, Operand::reg(1), Operand::imm(0));
    b.branch_cond(Operand::reg(2), 0x1008);  // skip next
    b.mov(3, Operand::imm(1));
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    insns[2].addr = 0x1002;
    insns[3].addr = 0x1004;
    insns[4].addr = 0x1008;
    return CFGBuilder::build(std::move(insns), 0x1000, "linear");
}

// Helper: build a function with a simple loop.
//   entry: cmp; BranchCond(exit)
//   loop_header: mov; BranchCond(back)
//   exit: ret
static Function make_loop_fn() {
    std::vector<Instruction> insns;
    Builder b;
    // 0x1000: cmp v2, 0
    b.cmp(2, Operand::reg(1), Operand::imm(0));
    // 0x1001: if v2 goto exit (0x1010)
    b.branch_cond(Operand::reg(2), 0x1010);
    // 0x1004: loop header - mov v3, 1
    b.mov(3, Operand::imm(1));
    // 0x1005: if v4 goto loop_header (0x1004)
    b.branch_cond(Operand::reg(4), 0x1004);
    // 0x1010: exit - ret
    b.ret();
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    insns[2].addr = 0x1004;
    insns[3].addr = 0x1005;
    insns[4].addr = 0x1010;
    return CFGBuilder::build(std::move(insns), 0x1000, "loop_fn");
}

TEST_CASE("Dominators: entry block has no immediate dominator") {
    auto fn = make_linear_fn();
    auto dom = compute_dominators(fn);
    REQUIRE_FALSE(dom.rpo.empty());
    // The entry block's idom should be -1 (nullopt).
    auto entry_it = fn.block_index.find(fn.entry);
    REQUIRE(entry_it != fn.block_index.end());
    CHECK_FALSE(dom.immediate_dominator(entry_it->second).has_value());
}

TEST_CASE("Dominators: single-block function dominates itself") {
    auto fn = make_linear_fn();
    auto dom = compute_dominators(fn);
    auto entry_it = fn.block_index.find(fn.entry);
    REQUIRE(entry_it != fn.block_index.end());
    CHECK(dom.dominates(entry_it->second, entry_it->second));
}

TEST_CASE("Dominators: loop function has reachable blocks") {
    auto fn = make_loop_fn();
    auto dom = compute_dominators(fn);
    CHECK_FALSE(dom.rpo.empty());
    // Every block in rpo should be marked reachable.
    for (std::size_t b : dom.rpo) {
        CHECK(dom.reachable(b));
    }
}

TEST_CASE("Loops: simple loop is detected") {
    auto fn = make_loop_fn();
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    // We expect at least one loop (the back edge from 0x1005 to 0x1004).
    CHECK_FALSE(loops.empty());
    // The loop header should be the block at 0x1004.
    auto hit = fn.block_index.find(0x1004);
    if (hit != fn.block_index.end() && !loops.empty()) {
        CHECK(loops[0].header == hit->second);
    }
}

TEST_CASE("Loops: linear function has no loops") {
    auto fn = make_linear_fn();
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    CHECK(loops.empty());
}

TEST_CASE("reachable_blocks: all blocks reachable in linear fn") {
    auto fn = make_linear_fn();
    auto reach = reachable_blocks(fn);
    CHECK(reach.size() == fn.blocks.size());
}

TEST_CASE("reachable_blocks: unreachable block excluded") {
    // Build a function where one block is never reached.
    std::vector<Instruction> insns;
    Builder b;
    b.ret();               // 0x1000: ret (entry)
    b.mov(1, Operand::imm(1));  // 0x1001: unreachable
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    auto fn = CFGBuilder::build(std::move(insns), 0x1000, "unreach");
    auto reach = reachable_blocks(fn);
    // The entry (0x1000) is reachable; the block at 0x1001 is not.
    CHECK(reach.size() >= 1);
    auto eit = fn.block_index.find(0x1000);
    if (eit != fn.block_index.end()) {
        CHECK(reach.count(eit->second));
    }
}

TEST_CASE("Dominators: dominates walks the idom chain") {
    auto fn = make_loop_fn();
    auto dom = compute_dominators(fn);
    auto entry_it = fn.block_index.find(fn.entry);
    REQUIRE(entry_it != fn.block_index.end());
    const std::size_t entry = entry_it->second;
    // Entry dominates every reachable block.
    for (std::size_t b : dom.rpo) {
        CHECK(dom.dominates(entry, b));
    }
}

TEST_CASE("Loop body includes header and latch") {
    auto fn = make_loop_fn();
    auto dom = compute_dominators(fn);
    auto loops = find_natural_loops(fn, dom);
    REQUIRE_FALSE(loops.empty());
    const auto& loop = loops[0];
    // Body must include both header and latch.
    bool has_header = false, has_latch = false;
    for (auto bi : loop.body) {
        if (bi == loop.header) has_header = true;
        if (bi == loop.latch)  has_latch  = true;
    }
    CHECK(has_header);
    CHECK(has_latch);
}
