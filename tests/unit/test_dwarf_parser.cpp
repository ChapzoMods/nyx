// =============================================================================
// Nyx unit tests: DWARF parser (v0.0.6)
// =============================================================================
#include "nyx/parsers/dwarf_parser.hpp"

#include <doctest/doctest.h>

TEST_CASE("DWARF: parse_dwarf on empty binary returns empty info") {
    nyx::BinaryInfo bin;
    nyx::ByteView data;
    auto info = nyx::parse_dwarf(bin, data);
    CHECK_FALSE(info.has_info);
    CHECK(info.lines.empty());
    CHECK(info.functions.empty());
}

TEST_CASE("DWARF: DwarfInfo::lookup_address on empty lines") {
    nyx::DwarfInfo info;
    auto e = info.lookup_address(0x1000);
    CHECK_FALSE(e.has_value());
}

TEST_CASE("DWARF: DwarfInfo::function_name_at on empty functions") {
    nyx::DwarfInfo info;
    auto n = info.function_name_at(0x1000);
    CHECK_FALSE(n.has_value());
}

TEST_CASE("DWARF: DwarfInfo::file_name with empty table") {
    nyx::DwarfInfo info;
    CHECK(info.file_name(0).empty());
    CHECK(info.file_name(1).empty());
}

TEST_CASE("DWARF: DwarfInfo::resolve_type_name on empty types") {
    nyx::DwarfInfo info;
    CHECK(info.resolve_type_name(0) == "void");
    CHECK(info.resolve_type_name(0xDEAD) == "void*");
}

TEST_CASE("DWARF: lookup_address finds entry at or before address") {
    nyx::DwarfInfo info;
    info.lines.push_back({0x1000, 1, 5, 0, true, false});
    info.lines.push_back({0x1004, 1, 6, 0, true, false});
    info.lines.push_back({0x1008, 1, 7, 0, true, false});

    auto e = info.lookup_address(0x1004);
    REQUIRE(e.has_value());
    CHECK(e->line == 6);

    auto e2 = info.lookup_address(0x1006);  // between entries
    REQUIRE(e2.has_value());
    CHECK(e2->line == 6);  // should round down to 0x1004

    auto e3 = info.lookup_address(0x0FFF);  // before first
    CHECK_FALSE(e3.has_value());
}

TEST_CASE("DWARF: function_name_at finds containing function") {
    nyx::DwarfInfo info;
    info.functions.push_back({"main", 0x1000, 0x1100, 0, true});
    info.functions.push_back({"helper", 0x1100, 0x1150, 0, true});

    CHECK(info.function_name_at(0x1050).value() == "main");
    CHECK(info.function_name_at(0x1100).value() == "helper");
    CHECK(info.function_name_at(0x1099).value() == "main");
    CHECK_FALSE(info.function_name_at(0x2000).has_value());
}

TEST_CASE("DWARF: resolve_type_name follows pointer chain") {
    nyx::DwarfInfo info;
    info.types[0x100] = {0x100, "int", nyx::DwarfType::Kind::Base, 4, 0};
    info.types[0x200] = {0x200, "", nyx::DwarfType::Kind::Pointer, 8, 0x100};
    info.types[0x300] = {0x300, "IntPtr", nyx::DwarfType::Kind::Typedef, 0, 0x200};

    CHECK(info.resolve_type_name(0x100) == "int");
    CHECK(info.resolve_type_name(0x200) == "int*");
    CHECK(info.resolve_type_name(0x300) == "int*");  // typedef resolved
    CHECK(info.resolve_type_name(0) == "void");
}

TEST_CASE("DWARF: resolve_type_name handles struct") {
    nyx::DwarfInfo info;
    info.types[0x400] = {0x400, "point", nyx::DwarfType::Kind::Struct, 8, 0};
    CHECK(info.resolve_type_name(0x400) == "struct point");
}

TEST_CASE("DWARF: robustness - corrupt section doesn't crash") {
    // Build a fake BinaryInfo with a corrupt .debug_line section.
    nyx::BinaryInfo bin;
    bin.endian = nyx::Endian::Little;
    nyx::Section s;
    s.name = ".debug_line";
    s.file_off = 0;
    s.file_size = 10;
    s.is_nobits = false;
    bin.sections.push_back(s);

    // Corrupt data: all zeros.
    const std::uint8_t data[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto info = nyx::parse_dwarf(bin, nyx::ByteView{data, 10});
    // Should not crash; lines should be empty (unit_len=0 means end).
    CHECK(info.lines.empty());
}
