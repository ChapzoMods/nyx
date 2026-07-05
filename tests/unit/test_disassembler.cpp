// =============================================================================
// Nyx unit tests: Disassembler (Capstone wrapper)
// =============================================================================
#include "nyx/parsers/disassembler.hpp"

#include <doctest/doctest.h>

#include <vector>

TEST_CASE("Disassembler: x86-64 decodes a known sequence") {
    // 48 89 e5               mov rbp, rsp
    // 48 89 7d f8            mov [rbp-0x8], rdi
    // 5d                     pop rbp
    // c3                     ret
    const std::uint8_t code[] = {0x48, 0x89, 0xe5,
                                 0x48, 0x89, 0x7d, 0xf8,
                                 0x5d,
                                 0xc3};
    nyx::Disassembler d(nyx::Arch::X86_64, nyx::Endian::Little);
    REQUIRE(d.valid());

    auto insns = d.decode(nyx::ByteView{code, sizeof(code)}, 0x401000);
    REQUIRE(insns.size() == 4);

    CHECK(insns[0].mnemonic == "mov");
    CHECK(insns[0].size     == 3);
    CHECK(insns[0].address  == 0x401000);

    CHECK(insns[3].mnemonic == "ret");
    CHECK(insns[3].is_ret());
    CHECK(insns[3].is_control_flow());
}

TEST_CASE("Disassembler: x86-64 direct call target") {
    // e8 17 00 00 00       call 0x401020  (rel32 = 0x17)
    const std::uint8_t code[] = {0xe8, 0x17, 0x00, 0x00, 0x00};
    nyx::Disassembler d(nyx::Arch::X86_64, nyx::Endian::Little);
    auto insns = d.decode(nyx::ByteView{code, sizeof(code)}, 0x401000);
    REQUIRE(insns.size() == 1);
    CHECK(insns[0].mnemonic == "call");
    CHECK(insns[0].is_call());
    auto tgt = insns[0].direct_target();
    REQUIRE(tgt.has_value());
    CHECK(*tgt == 0x40101C);  // 0x401000 + 5 + 0x17
}

TEST_CASE("Disassembler: x86-64 conditional jmp") {
    // 74 0a     jz 0x40100c
    const std::uint8_t code[] = {0x74, 0x0a};
    nyx::Disassembler d(nyx::Arch::X86_64, nyx::Endian::Little);
    auto insns = d.decode(nyx::ByteView{code, sizeof(code)}, 0x401000);
    REQUIRE(insns.size() == 1);
    CHECK(insns[0].mnemonic == "je");
    CHECK(insns[0].is_jump());
    CHECK(insns[0].is_conditional_branch());
    auto tgt = insns[0].direct_target();
    REQUIRE(tgt.has_value());
    CHECK(*tgt == 0x40100c);
}

TEST_CASE("Disassembler: invalid arch returns invalid handle") {
    nyx::Disassembler d(nyx::Arch::Unknown, nyx::Endian::Little);
    CHECK_FALSE(d.valid());
    auto insns = d.decode(nyx::ByteView{}, 0);
    CHECK(insns.empty());
}

TEST_CASE("Disassembler: decode_one on empty input") {
    nyx::Disassembler d(nyx::Arch::X86_64, nyx::Endian::Little);
    auto r = d.decode_one(nyx::ByteView{}, 0);
    CHECK_FALSE(r.has_value());
}
