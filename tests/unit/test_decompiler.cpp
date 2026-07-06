// =============================================================================
// Nyx unit tests: Decompiler pipeline
// =============================================================================
#include "nyx/decompiler/decompiler.hpp"

#include <doctest/doctest.h>

#include <string>

#ifndef NYX_FIXTURES_DIR
#  define NYX_FIXTURES_DIR "tests/fixtures"
#endif

TEST_CASE("Decompiler: returns empty vector for unsupported arch") {
    nyx::BinaryInfo bin;
    bin.arch   = nyx::Arch::Unknown;
    bin.endian = nyx::Endian::Little;

    nyx::Decompiler::Options opts;
    opts.linear_sweep_fallback = true;
    nyx::Decompiler dec(opts);
    auto funcs = dec.decompile(bin);
    CHECK(funcs.empty());
}

TEST_CASE("Decompiler: decompile_range produces a function with at least one block") {
    // 48 89 e5       mov rbp, rsp
    // 5d             pop rbp
    // c3             ret
    const std::uint8_t code[] = {0x48, 0x89, 0xe5, 0x5d, 0xc3};

    nyx::BinaryInfo bin;
    bin.arch   = nyx::Arch::X86_64;
    bin.endian = nyx::Endian::Little;

    nyx::Decompiler dec;
    auto fn = dec.decompile_range(bin, 0x401000, nyx::ByteView{code, sizeof(code)}, "test_fn");
    CHECK(fn.name == "test_fn");
    CHECK(fn.entry == 0x401000);
    CHECK(fn.block_count >= 1);
    CHECK(fn.insn_count  >= 1);
    CHECK_FALSE(fn.lines.empty());
}
