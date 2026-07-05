// =============================================================================
// Nyx integration tests: v0.0.4 features (dot output, type inference)
// =============================================================================
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/decompiler/type_inferer.hpp"
#include "nyx/output/dot_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

namespace fs = std::filesystem;

TEST_CASE("Integration: dot output on ARM64 Mach-O produces valid digraph") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    const std::string dot = nyx::output::to_dot(ir_fns);
    CHECK(dot.find("digraph") != std::string::npos);
    CHECK(dot.find("rankdir=TB") != std::string::npos);
    // The ARM64 fixture has mov/add/ret; the dot should reference vN.
    CHECK(dot.find("v1") != std::string::npos);
    CHECK(dot.find("return;") != std::string::npos);
}

TEST_CASE("Integration: dot output on ELF has nodes and edges") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    const std::string dot = nyx::output::to_dot(ir_fns);
    CHECK(dot.find("digraph") != std::string::npos);
    // Multiple functions => at least one "fn" node.
    CHECK(dot.find("bb_") != std::string::npos);
    // At least one edge.
    CHECK(dot.find("->") != std::string::npos);
}

TEST_CASE("Integration: type inference populates vreg_types on ARM64") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    // The ARM64 fixture has mov x0,#0; mov x1,#1; add x2,x0,x1; ret.
    // After inference, v1 and v2 should be Int32 (small imms), v3 Int32.
    const auto& fn = ir_fns.front();
    CHECK_FALSE(fn.vreg_types.empty());
    // v1 (mov #0) should be Int32.
    auto it = fn.vreg_types.find(1);
    if (it != fn.vreg_types.end()) {
        CHECK(it->second == nyx::ir::Type::Int32);
    }
}

TEST_CASE("Integration: pseudo-C emits typed declarations") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.arm64.macho";
    REQUIRE(fs::exists(path));

    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);
    nyx::Decompiler dec;
    auto ir_fns = dec.decompile_ir(bin);
    REQUIRE_FALSE(ir_fns.empty());

    const std::string body = nyx::render_pseudo_c(ir_fns.front());
    // The function should declare at least one variable with a type.
    CHECK(body.find("int v") != std::string::npos);
}

TEST_CASE("Integration: CLI --format dot produces parseable Graphviz") {
    const std::string path = std::string(NYX_FIXTURES_DIR) + "/sample.elf";
    REQUIRE(fs::exists(path));

    // Use popen to invoke the CLI binary.
    const fs::path bin = fs::path(NYX_BINARY_DIR) / "nyx";
    REQUIRE(fs::exists(bin));
    std::string cmd = bin.string() + " --format dot --log-level error " + path + " 2>&1";
    std::string out;
    char buf[4096];
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe);
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    pclose(pipe);

    CHECK(out.find("digraph") != std::string::npos);
    CHECK(out.find("rankdir=TB") != std::string::npos);
    CHECK(out.find("bb_") != std::string::npos);
}
