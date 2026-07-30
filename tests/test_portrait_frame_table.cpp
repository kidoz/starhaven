// Tests for DPFT.BIN.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "core/world/portrait_frame_table.hpp"

using namespace starhaven::world;

namespace {

struct PortraitSpec {
    std::uint16_t id = 0;
    std::uint16_t cell_id = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t count = 0;
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::byte> make_entry(const std::vector<PortraitSpec>& frames,
                                  bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + frames.size() * kPortraitFrameRecordSize, 0);
    const auto count =
        static_cast<std::uint32_t>(corrupt_count ? frames.size() + 1 : frames.size());
    put_u16(raw, 0, count);

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::size_t base = 4 + i * kPortraitFrameRecordSize;
        put_u16(raw, base + 0, frames[i].id);
        put_u16(raw, base + 2, frames[i].cell_id);
        put_u16(raw, base + 4, frames[i].width);
        put_u16(raw, base + 6, frames[i].height);
        put_u16(raw, base + 8, frames[i].count);
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

TEST_CASE("portrait frame table decodes all five u16", "[portrait_frame_table]") {
    const PortraitSpec first{.id = 0, .cell_id = 1, .width = 8, .height = 8, .count = 4};
    const PortraitSpec second{.id = 46, .cell_id = 46, .width = 16, .height = 16, .count = 4};

    PortraitFrameTable table;
    REQUIRE(PortraitFrameTable::parse(make_entry({first, second}), table) ==
            PortraitFrameTableError::None);
    REQUIRE(table.size() == 2);
    REQUIRE(table.entries()[0].id == 0);
    REQUIRE(table.entries()[0].cell_id == 1);
    REQUIRE(table.entries()[0].width == 8);
    REQUIRE(table.entries()[0].height == 8);
    REQUIRE(table.entries()[0].count == 4);
    REQUIRE(table.entries()[1].width == 16);
    REQUIRE(table.entries()[1].height == 16);
}

TEST_CASE("portrait frame table rejects malformed input", "[portrait_frame_table]") {
    PortraitFrameTable table;
    REQUIRE(PortraitFrameTable::parse(std::vector<std::byte>(16), table) ==
            PortraitFrameTableError::TooSmall);
    REQUIRE(PortraitFrameTable::parse(std::vector<std::byte>(80, std::byte{0xAB}), table) ==
            PortraitFrameTableError::NotCompressed);
    REQUIRE(PortraitFrameTable::parse(make_entry({{0, 1, 8, 8, 4}}, true), table) ==
            PortraitFrameTableError::BadCount);
}
