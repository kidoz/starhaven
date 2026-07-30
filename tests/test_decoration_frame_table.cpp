// Tests for DIFT.BIN.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/world/decoration_frame_table.hpp"

using namespace starhaven::world;

namespace {

struct DecorationSpec {
    std::string group_name;   // set on a group's first frame
    std::string sprite_name;
    std::uint16_t flags = 0;
    std::uint16_t frame_count = 0;
    std::uint16_t duration = 0;
    std::uint16_t tail = 0;  // zero on every shipped frame
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::byte> make_entry(const std::vector<DecorationSpec>& frames,
                                  bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + frames.size() * kDecorationFrameRecordSize, 0);
    const auto count =
        static_cast<std::uint32_t>(corrupt_count ? frames.size() + 1 : frames.size());
    put_u16(raw, 0, count);

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const DecorationSpec& frame = frames[i];
        const std::size_t base = 4 + i * kDecorationFrameRecordSize;
        for (std::size_t k = 0; k < frame.group_name.size() && k < kDecorationNameSize; ++k) {
            raw[base + k] = static_cast<std::uint8_t>(frame.group_name[k]);
        }
        const std::size_t sprite = base + kDecorationNameSize;
        for (std::size_t k = 0; k < frame.sprite_name.size() && k < kDecorationNameSize; ++k) {
            raw[sprite + k] = static_cast<std::uint8_t>(frame.sprite_name[k]);
        }
        put_u16(raw, base + 0x18, frame.flags);
        put_u16(raw, base + 0x1A, frame.frame_count);
        put_u16(raw, base + 0x1C, frame.duration);
        put_u16(raw, base + 0x1E, frame.tail);
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

TEST_CASE("decoration frames decode the DSFT shape", "[decoration_frame_table]") {
    const DecorationSpec head{.group_name = "glow01",
                              .sprite_name = "glow01a",
                              .flags = 1,
                              .frame_count = 6,
                              .duration = 5};
    const DecorationSpec mid{.sprite_name = "glow01b", .flags = 1, .duration = 1};

    DecorationFrameTable table;
    REQUIRE(DecorationFrameTable::parse(make_entry({head, mid}), table) ==
            DecorationFrameTableError::None);
    REQUIRE(table.size() == 2);
    REQUIRE(table.entries()[0].group_name == "glow01");
    REQUIRE(table.entries()[0].sprite_name == "glow01a");
    REQUIRE(table.entries()[0].flags == 1);
    REQUIRE(table.entries()[0].frame_count == 6);
    REQUIRE(table.entries()[0].duration == 5);
    // A frame that does not start a group carries an empty group name.
    REQUIRE(table.entries()[1].group_name.empty());
    REQUIRE(table.entries()[1].sprite_name == "glow01b");
    REQUIRE(table.entries()[1].duration == 1);
}

TEST_CASE("decoration frame table rejects malformed input", "[decoration_frame_table]") {
    DecorationFrameTable table;
    REQUIRE(DecorationFrameTable::parse(std::vector<std::byte>(16), table) ==
            DecorationFrameTableError::TooSmall);
    REQUIRE(DecorationFrameTable::parse(std::vector<std::byte>(80, std::byte{0xAB}), table) ==
            DecorationFrameTableError::NotCompressed);
    REQUIRE(DecorationFrameTable::parse(make_entry({{"g", "s", 1, 1, 1}}, true), table) ==
            DecorationFrameTableError::BadCount);
}
