// =============================================================================
// Nyx unit tests: FunctionDetector v0.0.4 extensions (new prologues + end)
// =============================================================================
#include "nyx/decompiler/function_detector.hpp"

#include <doctest/doctest.h>

TEST_CASE("FunctionDetector v0.0.4: ARM32 stmfd prologue") {
    nyx::DecodedInstruction d;
    d.mnemonic = "stmfd";
    d.op_str = "sp!, {fp, ip, lr, pc}";
    d.address = 0x8000;
    nyx::FunctionDetector det(nyx::Arch::ARM);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
    CHECK(cands[0].address == 0x8000);
}

TEST_CASE("FunctionDetector v0.0.4: ARM32 str lr prologue") {
    nyx::DecodedInstruction d;
    d.mnemonic = "str";
    d.op_str = "lr, [sp, #-4]!";
    d.address = 0x8000;
    nyx::FunctionDetector det(nyx::Arch::ARM);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: ARM32 add ip, sp prologue (PIC)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "add";
    d.op_str = "ip, sp, #0";
    d.address = 0x8000;
    nyx::FunctionDetector det(nyx::Arch::ARM);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: PPC stfd prologue") {
    nyx::DecodedInstruction d;
    d.mnemonic = "stfd";
    d.op_str = "f31, -8(r1)";
    d.address = 0x10001000;
    nyx::FunctionDetector det(nyx::Arch::PPC);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: MIPS sw gp prologue (PIC)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "sw";
    d.op_str = "$gp, 16($sp)";
    d.address = 0x4001000;
    nyx::FunctionDetector det(nyx::Arch::MIPS);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: MIPS lui gp prologue (PIC)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "lui";
    d.op_str = "$gp, 0x1234";
    d.address = 0x4001000;
    nyx::FunctionDetector det(nyx::Arch::MIPS);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: MIPS sd ra prologue (MIPS64)") {
    nyx::DecodedInstruction d;
    d.mnemonic = "sd";
    d.op_str = "$ra, 8($sp)";
    d.address = 0x4001000;
    nyx::FunctionDetector det(nyx::Arch::MIPS64);
    auto cands = det.detect({d});
    CHECK(cands.size() == 1);
}

TEST_CASE("FunctionDetector v0.0.4: is_return x86-64 ret") {
    nyx::DecodedInstruction d;
    d.mnemonic = "ret";
    d.op_str = "";
    d.address = 0x1000;
    std::vector<nyx::DecodedInstruction> insns = {d};
    nyx::FunctionDetector det(nyx::Arch::X86_64);
    // find_function_end returns the index AFTER the return.
    CHECK(det.find_function_end(insns, 0) == 1);
}

TEST_CASE("FunctionDetector v0.0.4: is_return ARM32 bx lr") {
    nyx::DecodedInstruction d;
    d.mnemonic = "bx";
    d.op_str = "lr";
    d.address = 0x8000;
    std::vector<nyx::DecodedInstruction> insns = {d};
    nyx::FunctionDetector det(nyx::Arch::ARM);
    CHECK(det.find_function_end(insns, 0) == 1);
}

TEST_CASE("FunctionDetector v0.0.4: is_return ARM32 pop {pc}") {
    nyx::DecodedInstruction d;
    d.mnemonic = "pop";
    d.op_str = "{pc}";
    d.address = 0x8000;
    std::vector<nyx::DecodedInstruction> insns = {d};
    nyx::FunctionDetector det(nyx::Arch::ARM);
    CHECK(det.find_function_end(insns, 0) == 1);
}

TEST_CASE("FunctionDetector v0.0.4: is_return PPC blr") {
    nyx::DecodedInstruction d;
    d.mnemonic = "blr";
    d.op_str = "";
    d.address = 0x10001000;
    std::vector<nyx::DecodedInstruction> insns = {d};
    nyx::FunctionDetector det(nyx::Arch::PPC);
    CHECK(det.find_function_end(insns, 0) == 1);
}

TEST_CASE("FunctionDetector v0.0.4: is_return MIPS jr $ra") {
    nyx::DecodedInstruction d;
    d.mnemonic = "jr";
    d.op_str = "$ra";
    d.address = 0x4001000;
    std::vector<nyx::DecodedInstruction> insns = {d};
    nyx::FunctionDetector det(nyx::Arch::MIPS);
    CHECK(det.find_function_end(insns, 0) == 1);
}

TEST_CASE("FunctionDetector v0.0.4: find_function_end stops at next prologue") {
    // Block: push rbp; mov rbp,rsp; ret; push rbp; ret
    std::vector<nyx::DecodedInstruction> insns(5);
    insns[0].mnemonic = "push"; insns[0].op_str = "rbp"; insns[0].address = 0x1000;
    insns[1].mnemonic = "mov";  insns[1].op_str = "rbp, rsp"; insns[1].address = 0x1001;
    insns[2].mnemonic = "ret";  insns[2].op_str = "";          insns[2].address = 0x1003;
    insns[3].mnemonic = "push"; insns[3].op_str = "rbp"; insns[3].address = 0x1004;
    insns[4].mnemonic = "ret";  insns[4].op_str = "";          insns[4].address = 0x1005;

    nyx::FunctionDetector det(nyx::Arch::X86_64);
    // find_function_end starting at index 0 should stop at the ret (index 2),
    // returning 3 (one past the ret).
    CHECK(det.find_function_end(insns, 0) == 3);
    // Starting at index 3 (second prologue) should stop at the ret (index 4).
    CHECK(det.find_function_end(insns, 3) == 5);
}

TEST_CASE("FunctionDetector v0.0.4: find_function_end returns size when no return") {
    std::vector<nyx::DecodedInstruction> insns(2);
    insns[0].mnemonic = "push"; insns[0].op_str = "rbp"; insns[0].address = 0x1000;
    insns[1].mnemonic = "mov";  insns[1].op_str = "rbp, rsp"; insns[1].address = 0x1001;
    nyx::FunctionDetector det(nyx::Arch::X86_64);
    CHECK(det.find_function_end(insns, 0) == 2);
}
