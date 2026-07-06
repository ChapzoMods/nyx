// =============================================================================
// Nyx unit tests: arch.hpp
// =============================================================================
#include "nyx/core/arch.hpp"

#include <doctest/doctest.h>

TEST_CASE("arch_bitness: x86/x86-64/arm/arm64/ppc/mips") {
    CHECK(nyx::arch_bitness(nyx::Arch::X86)     == 32);
    CHECK(nyx::arch_bitness(nyx::Arch::X86_64)  == 64);
    CHECK(nyx::arch_bitness(nyx::Arch::ARM)     == 32);
    CHECK(nyx::arch_bitness(nyx::Arch::AARCH64) == 64);
    CHECK(nyx::arch_bitness(nyx::Arch::PPC)     == 32);
    CHECK(nyx::arch_bitness(nyx::Arch::PPC64)   == 64);
    CHECK(nyx::arch_bitness(nyx::Arch::MIPS)    == 32);
    CHECK(nyx::arch_bitness(nyx::Arch::MIPS64)  == 64);
}

TEST_CASE("arch_name: round-trip with arch_from_name") {
    for (auto a : {nyx::Arch::X86, nyx::Arch::X86_64, nyx::Arch::ARM,
                   nyx::Arch::AARCH64, nyx::Arch::PPC, nyx::Arch::PPC64,
                   nyx::Arch::MIPS, nyx::Arch::MIPS64}) {
        const auto name = nyx::arch_name(a);
        const auto back = nyx::arch_from_name(name);
        REQUIRE(back.has_value());
        CHECK(*back == a);
    }
}

TEST_CASE("arch_from_name: aliases are case-insensitive") {
    CHECK(nyx::arch_from_name("AMD64").value() == nyx::Arch::X86_64);
    CHECK(nyx::arch_from_name("x64").value()   == nyx::Arch::X86_64);
    CHECK(nyx::arch_from_name("i386").value()  == nyx::Arch::X86);
    CHECK(nyx::arch_from_name("ARMv7").value() == nyx::Arch::ARM);
    CHECK_FALSE(nyx::arch_from_name("invalid").has_value());
}

TEST_CASE("arch_default_endian: PPC/MIPS are big-endian by default") {
    CHECK(nyx::arch_default_endian(nyx::Arch::PPC)    == nyx::Endian::Big);
    CHECK(nyx::arch_default_endian(nyx::Arch::MIPS)   == nyx::Endian::Big);
    CHECK(nyx::arch_default_endian(nyx::Arch::X86_64) == nyx::Endian::Little);
    CHECK(nyx::arch_default_endian(nyx::Arch::AARCH64) == nyx::Endian::Little);
}

TEST_CASE("capstone_map::for_arch: x86 / x86-64 / unknown") {
    auto c1 = nyx::capstone_map::for_arch(nyx::Arch::X86, nyx::Endian::Little);
    REQUIRE(c1.has_value());
    CHECK(c1->little_endian);

    auto c2 = nyx::capstone_map::for_arch(nyx::Arch::X86_64, nyx::Endian::Little);
    REQUIRE(c2.has_value());

    auto c3 = nyx::capstone_map::for_arch(nyx::Arch::Unknown, nyx::Endian::Little);
    CHECK_FALSE(c3.has_value());
}
