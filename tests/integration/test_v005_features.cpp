// =============================================================================
// Nyx integration tests: v0.0.5 features (dot with loops, pseudo-C while)
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/output/dot_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: dot output with loop annotations on ELF") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    // For each function, compute dominators + loops and render dot.
    for (const auto& fn : ir_fns) {
        auto dom = nyx::ir::compute_dominators(fn);
        auto loops = nyx::ir::find_natural_loops(fn, dom);
        const std::string dot = nyx::output::to_dot(fn, dom, loops);
        CHECK(dot.find("digraph") != std::string::npos);
        // The dot should have at least one node.
        CHECK(dot.find("bb_") != std::string::npos);
    }
}

TEST_CASE("Integration: pseudo-C with loop annotations") {
    // Build a function with a back edge manually.
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.cmp(2, nyx::ir::Operand::reg(1), nyx::ir::Operand::imm(0));
    b.branch_cond(nyx::ir::Operand::reg(2), 0x1010);  // exit
    b.mov(3, nyx::ir::Operand::imm(1));                 // 0x1004: loop header
    b.branch_cond(nyx::ir::Operand::reg(4), 0x1004);  // back edge
    b.ret();                                            // 0x1010: exit
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    insns[2].addr = 0x1004;
    insns[3].addr = 0x1005;
    insns[4].addr = 0x1010;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "loop_test");
    auto dom = nyx::ir::compute_dominators(fn);
    auto loops = nyx::ir::find_natural_loops(fn, dom);

    const std::string body = nyx::render_pseudo_c(fn, dom, loops);
    // The loop should produce a `while (1)` annotation.
    CHECK(body.find("while (1)") != std::string::npos);
    // The back edge should be rendered as `continue;`.
    CHECK(body.find("continue;") != std::string::npos);
}

TEST_CASE("Integration: dominators on ARM64 fixture") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    auto dom = nyx::ir::compute_dominators(ir_fns[0]);
    // The ARM64 fixture has a single block (no loops), so dominators
    // should mark it as the entry with no idom.
    CHECK_FALSE(dom.rpo.empty());
    CHECK(dom.idom.size() == ir_fns[0].blocks.size());
}

TEST_CASE("Integration: dot output colours entry node") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    const auto& fn = ir_fns[0];
    auto dom = nyx::ir::compute_dominators(fn);
    auto loops = nyx::ir::find_natural_loops(fn, dom);
    const std::string dot = nyx::output::to_dot(fn, dom, loops);
    // The entry node should be coloured lightgreen.
    CHECK(dot.find("lightgreen") != std::string::npos);
}

TEST_CASE("Integration: unreachable block pruning via reachable_blocks") {
    // Build a function with an unreachable block.
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.ret();                            // 0x1000: entry, returns
    b.mov(1, nyx::ir::Operand::imm(1));  // 0x1001: unreachable
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "unreach");

    auto reach = nyx::ir::reachable_blocks(fn);
    // The unreachable block should NOT be in the reachable set.
    auto ub = fn.block_index.find(0x1001);
    if (ub != fn.block_index.end()) {
        CHECK(reach.count(ub->second) == 0);
    }
    // The entry should be reachable.
    auto eb = fn.block_index.find(0x1000);
    if (eb != fn.block_index.end()) {
        CHECK(reach.count(eb->second) == 1);
    }
}
