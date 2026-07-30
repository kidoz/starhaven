// Tests for DOVERLAY.BIN.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "core/world/overlay_table.hpp"

using namespace starhaven::world;

namespace {

struct OverlaySpec {
    std::uint32_t id = 0;
    std::uint16_t scale = 0;
    std::uint16_t tail = 0;  // zero on every shipped row
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::vector<std::byte> make_entry(const std::vector<OverlaySpec>& rows,
                                  bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + rows.size() * kOverlayRecordSize, 0);
    const auto count = static_cast<std::uint32_t>(corrupt_count ? rows.size() + 1 : rows.size());
    put_u32(raw, 0, count);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t base = 4 + i * kOverlayRecordSize;
        put_u32(raw, base, rows[i].id);
        put_u16(raw, base + 4, rows[i].scale);
        put_u16(raw, base + 6, rows[i].tail);
    }

    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(bound);
    REQUIRE(compress2(compressed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()),
                      Z_DEFAULT_COMPRESSION) == Z_OK);
    compressed.resize(bound);

    std::vector<std::byte> entry(48 + compressed.size(), std::byte{0});
    std::memcpy(entry.data() + 48, compressed.data(), compressed.size());
    return entry;
}

}  // namespace

TEST_CASE("overlay table decodes ids and scales", "[overlay_table]") {
    const OverlaySpec first{.id = 1020, .scale = 512};
    const OverlaySpec second{.id = 2000, .scale = 524};

    OverlayTable table;
    REQUIRE(OverlayTable::parse(make_entry({first, second}), table) == OverlayTableError::None);
    REQUIRE(table.size() == 2);
    REQUIRE(table.entries()[0].id == 1020);
    REQUIRE(table.entries()[0].scale == 512);
    REQUIRE(table.entries()[1].id == 2000);
    REQUIRE(table.entries()[1].scale == 524);
}

TEST_CASE("overlay table rejects malformed input", "[overlay_table]") {
    OverlayTable table;
    REQUIRE(OverlayTable::parse(std::vector<std::byte>(16), table) == OverlayTableError::TooSmall);
    REQUIRE(OverlayTable::parse(std::vector<std::byte>(80, std::byte{0xAB}), table) ==
            OverlayTableError::NotCompressed);
    REQUIRE(OverlayTable::parse(make_entry({{1020, 512}}, true), table) ==
            OverlayTableError::BadCount);
}
