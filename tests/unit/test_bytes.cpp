// =============================================================================
// Nyx unit tests: bytes.hpp utilities
// =============================================================================
#include "nyx/core/bytes.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

TEST_CASE("ByteView: empty and basic access") {
    nyx::ByteView v;
    CHECK(v.empty());
    CHECK(v.size() == 0);

    const std::uint8_t data[] = {1, 2, 3, 4};
    nyx::ByteView w{data, 4};
    CHECK_FALSE(w.empty());
    CHECK(w.size() == 4);
    CHECK(w[0] == 1);
    CHECK(w[3] == 4);
}

TEST_CASE("ByteView: sub-view bounds") {
    const std::uint8_t data[] = {0, 1, 2, 3, 4, 5};
    nyx::ByteView v{data, 6};
    auto s = v.sub(2, 3);
    REQUIRE(s.has_value());
    CHECK(s->size() == 3);
    CHECK((*s)[0] == 2);

    CHECK_FALSE(v.sub(2, 100).has_value());
    CHECK_FALSE(v.sub(100, 1).has_value());
    CHECK(v.sub(0, 0).has_value());
}

TEST_CASE("read_u16/32/64 LE and BE") {
    const std::uint8_t le[] = {0x78, 0x56, 0x34, 0x12};
    CHECK(nyx::read_u32_le(le) == 0x12345678u);
    CHECK(nyx::read_u32_be(le) == 0x78563412u);

    const std::uint8_t be[] = {0x12, 0x34, 0x56, 0x78};
    CHECK(nyx::read_u32_be(be) == 0x12345678u);

    const std::uint8_t le64[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    CHECK(nyx::read_u64_le(le64) == 0x0000000100000000ull);
}

TEST_CASE("to_hex helpers") {
    CHECK(nyx::to_hex(static_cast<std::uint64_t>(0x1234), 0, true)  == "0x1234");
    CHECK(nyx::to_hex(static_cast<std::uint64_t>(0x1234), 8, true)  == "0x00001234");
    CHECK(nyx::to_hex(static_cast<std::uint64_t>(0x1234), 0, false) == "1234");

    const std::uint8_t bytes[] = {0x01, 0x02, 0x03};
    nyx::ByteView bv{bytes, 3};
    // bytes_per_group=0 means "no spaces between groups".
    CHECK(nyx::to_hex(bv, 0) == "010203");
    // bytes_per_group=1 means "space every byte after the first".
    CHECK(nyx::to_hex(bv, 1) == "01 02 03");
}

TEST_CASE("to_hex_dump: width and ascii column") {
    const std::uint8_t bytes[] = {'H', 'i', 0x00, 0x01};
    nyx::ByteView bv{bytes, 4};
    const std::string s = nyx::to_hex_dump(bv, 0, 16);
    CHECK(s.find("Hi") != std::string::npos);
    CHECK(s.find("0000000000000000  48 69 00 01") != std::string::npos);
}

TEST_CASE("ByteBuffer::from_file: missing file") {
    auto r = nyx::ByteBuffer::from_file("/nonexistent/path/xyz");
    CHECK_FALSE(r.has_value());
}
