// =============================================================================
// Nyx unit tests: Calling conventions (v0.1.0)
// =============================================================================
#include "nyx/decompiler/calling_convention.hpp"

#include <doctest/doctest.h>

TEST_CASE("CC: SysV AMD64 arg regs") {
    auto cc = nyx::default_calling_convention(nyx::Arch::X86_64);
    CHECK(cc.conv == nyx::CallingConvention::SysV_AMD64);
    CHECK(cc.arg_regs.size() == 6);
    CHECK(cc.arg_regs[0] == "rdi");
    CHECK(cc.arg_regs[1] == "rsi");
    CHECK(cc.return_reg == "rax");
    CHECK(cc.max_reg_args == 6);
}

TEST_CASE("CC: AAPCS ARM64 arg regs") {
    auto cc = nyx::default_calling_convention(nyx::Arch::AARCH64);
    CHECK(cc.conv == nyx::CallingConvention::AAPCS_ARM64);
    CHECK(cc.arg_regs.size() == 8);
    CHECK(cc.arg_regs[0] == "x0");
    CHECK(cc.arg_regs[1] == "x1");
    CHECK(cc.return_reg == "x0");
    CHECK(cc.max_reg_args == 8);
}

TEST_CASE("CC: AAPCS ARM32 arg regs") {
    auto cc = nyx::default_calling_convention(nyx::Arch::ARM);
    CHECK(cc.conv == nyx::CallingConvention::AAPCS_ARM32);
    CHECK(cc.arg_regs.size() == 4);
    CHECK(cc.arg_regs[0] == "r0");
    CHECK(cc.return_reg == "r0");
}

TEST_CASE("CC: MIPS O32 arg regs") {
    auto cc = nyx::default_calling_convention(nyx::Arch::MIPS);
    CHECK(cc.conv == nyx::CallingConvention::MIPS_O32);
    CHECK(cc.arg_regs.size() == 4);
    CHECK(cc.arg_regs[0] == "$a0");
    CHECK(cc.return_reg == "$v0");
}

TEST_CASE("CC: PPC SVR arg regs") {
    auto cc = nyx::default_calling_convention(nyx::Arch::PPC);
    CHECK(cc.conv == nyx::CallingConvention::PPC_SVR);
    CHECK(cc.arg_regs.size() == 8);
    CHECK(cc.arg_regs[0] == "r3");
    CHECK(cc.return_reg == "r3");
}

TEST_CASE("CC: register_to_param_index") {
    auto cc = nyx::default_calling_convention(nyx::Arch::X86_64);
    CHECK(nyx::register_to_param_index(cc, "rdi") == 0);
    CHECK(nyx::register_to_param_index(cc, "rsi") == 1);
    CHECK(nyx::register_to_param_index(cc, "rax") == std::nullopt);
    CHECK(nyx::register_to_param_index(cc, "r9") == 5);
}

TEST_CASE("CC: param_name and retval_name") {
    CHECK(nyx::param_name(0) == "param1");
    CHECK(nyx::param_name(1) == "param2");
    CHECK(nyx::param_name(5) == "param6");
    CHECK(nyx::retval_name() == "retval");
}

TEST_CASE("CC: to_string") {
    CHECK(nyx::to_string(nyx::CallingConvention::SysV_AMD64) == "sysv_amd64");
    CHECK(nyx::to_string(nyx::CallingConvention::AAPCS_ARM64) == "aapcs_arm64");
    CHECK(nyx::to_string(nyx::CallingConvention::Unknown) == "unknown");
}

TEST_CASE("CC: Unknown arch returns Unknown convention") {
    auto cc = nyx::default_calling_convention(nyx::Arch::Unknown);
    CHECK(cc.conv == nyx::CallingConvention::Unknown);
    CHECK(cc.arg_regs.empty());
}
