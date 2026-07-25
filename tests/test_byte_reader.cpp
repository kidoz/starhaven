// Tests for the bounds-checked, little-endian ByteReader.
//
// All fixtures are synthetic bytes built by hand. No game content is involved.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "core/io/byte_reader.hpp"

using namespace starhaven::io;

namespace {

ByteReader make_reader(std::initializer_list<std::uint8_t> bytes) {
    static thread_local std::vector<std::byte> storage;
    storage.assign(bytes.size(), std::byte{0});
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        storage[i] = static_cast<std::byte>(bytes.begin()[i]);
    }
    return ByteReader{std::span<const std::byte>{storage}};
}

}  // namespace

TEST_CASE("u8/u16/u32 read little-endian", "[byte_reader]") {
    auto r = make_reader({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80});

    REQUIRE(r.read_u8() == 0x10);
    REQUIRE(r.read_u8() == 0x20);
    REQUIRE(r.read_u16_le() == 0x4030);
    REQUIRE(r.read_u32_le() == 0x80706050);
    REQUIRE(r.ok());
    REQUIRE(r.eof());
}

TEST_CASE("read_i32_le preserves sign", "[byte_reader]") {
    // 0xFFFFFFFF == -1 as signed int32.
    auto r = make_reader({0xFF, 0xFF, 0xFF, 0xFF});
    REQUIRE(r.read_i32_le() == -1);
    REQUIRE(r.ok());
}

TEST_CASE("out-of-bounds read sets error and leaves cursor unchanged", "[byte_reader]") {
    auto r = make_reader({0x01, 0x02});
    REQUIRE(r.read_u16_le() == 0x0201);
    REQUIRE(r.ok());
    REQUIRE(r.position() == 2);

    // Past the end: returns 0, error set, cursor unchanged.
    REQUIRE(r.read_u8() == 0);
    REQUIRE(r.last_error() == ReadError::OutOfBounds);
    REQUIRE(r.position() == 2);
}

TEST_CASE("skip and seek respect bounds", "[byte_reader]") {
    auto r = make_reader({0, 0, 0, 0});

    REQUIRE_FALSE(r.skip(5));
    REQUIRE(r.last_error() == ReadError::OutOfBounds);
    REQUIRE(r.position() == 0);

    REQUIRE(r.seek(4));
    REQUIRE(r.eof());

    REQUIRE_FALSE(r.seek(5));
    REQUIRE(r.last_error() == ReadError::OutOfBounds);

    // Seeking to exactly size() (end-of-stream) is allowed.
    REQUIRE(r.seek(4));
}

TEST_CASE("read_bytes fills output and rejects undersized target", "[byte_reader]") {
    auto r = make_reader({0x01, 0x02, 0x03});
    std::byte out[2] = {};
    REQUIRE(r.read_bytes(out));
    REQUIRE(out[0] == std::byte{0x01});
    REQUIRE(out[1] == std::byte{0x02});
    REQUIRE(r.position() == 2);

    std::byte too_big[3] = {};
    REQUIRE_FALSE(r.read_bytes(too_big));
    REQUIRE(r.last_error() == ReadError::OutOfBounds);
    // cursor unchanged after failure
    REQUIRE(r.position() == 2);
}

TEST_CASE("read_fixed_string stops at NUL", "[byte_reader]") {
    auto r = make_reader({'a', 'b', '\0', 'x', 'y'});
    std::string s;
    REQUIRE(r.read_fixed_string(5, s));
    REQUIRE(s == "ab");
    REQUIRE(r.position() == 5);
}

TEST_CASE("read_fixed_string without NUL reads full width", "[byte_reader]") {
    auto r = make_reader({'H', 'e', 'l', 'l', 'o'});
    std::string s;
    REQUIRE(r.read_fixed_string(5, s));
    REQUIRE(s == "Hello");
}

TEST_CASE("read_fixed_string rejects truncated field", "[byte_reader]") {
    auto r = make_reader({'a', 'b'});
    std::string s;
    REQUIRE_FALSE(r.read_fixed_string(8, s));
    // The field width exceeds the whole buffer, so it is reported as a count
    // error rather than a plain out-of-bounds.
    REQUIRE(r.last_error() == ReadError::CountTooLarge);
}

TEST_CASE("reader is empty-constructible", "[byte_reader]") {
    ByteReader r;
    REQUIRE(r.size() == 0);
    REQUIRE(r.eof());
    REQUIRE_FALSE(r.seek(1));
}
