// Tests for DOBJLIST.BIN.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/world/object_table.hpp"

using namespace starhaven::world;

namespace {

struct DescriptorSpec {
    std::string name;
    std::uint16_t object_id = 0;
    std::uint16_t radius = 0;
    std::uint16_t height = 0;
    std::uint16_t flags = 0;
    std::uint16_t sprite_frame = 0;
    std::uint16_t lifetime = 0;
    std::uint16_t speed = 0;
    std::uint8_t red = 0, green = 0, blue = 0;
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::byte> make_entry(const std::vector<DescriptorSpec>& descriptors,
                                  bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + descriptors.size() * kObjectDescriptorSize, 0);
    const auto count =
        static_cast<std::uint32_t>(corrupt_count ? descriptors.size() + 1 : descriptors.size());
    for (int i = 0; i < 4; ++i) {
        raw[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((count >> (8 * i)) & 0xFF);
    }

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const DescriptorSpec& descriptor = descriptors[i];
        const std::size_t base = 4 + i * kObjectDescriptorSize;
        for (std::size_t k = 0; k < descriptor.name.size() && k < kObjectDescriptorNameSize; ++k) {
            raw[base + k] = static_cast<std::uint8_t>(descriptor.name[k]);
        }
        put_u16(raw, base + 0x20, descriptor.object_id);
        put_u16(raw, base + 0x22, descriptor.radius);
        put_u16(raw, base + 0x24, descriptor.height);
        put_u16(raw, base + 0x26, descriptor.flags);
        put_u16(raw, base + 0x28, descriptor.sprite_frame);
        put_u16(raw, base + 0x2A, descriptor.lifetime);
        put_u16(raw, base + 0x2E, descriptor.speed);
        raw[base + 0x30] = descriptor.red;
        raw[base + 0x31] = descriptor.green;
        raw[base + 0x32] = descriptor.blue;
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

TEST_CASE("object descriptors decode the DOBJ to DSFT join", "[object_table]") {
    const DescriptorSpec deck{
        .name = "Deck of Fate",
        .object_id = 76,
        .radius = 64,
        .height = 16,
        .sprite_frame = 795,
    };
    const DescriptorSpec projectile{
        .name = "explosion",
        .object_id = 561,
        .radius = 16,
        .height = 16,
        .flags = 60,
        .sprite_frame = 602,
        .lifetime = 96,
        .speed = 4000,
        .red = 255,
        .green = 128,
        .blue = 20,
    };

    ObjectTable table;
    REQUIRE(ObjectTable::parse(make_entry({deck, projectile}), table) == ObjectTableError::None);
    REQUIRE(table.size() == 2);
    REQUIRE(table.at(0)->name == "Deck of Fate");
    REQUIRE(table.at(0)->object_id == 76);
    REQUIRE(table.at(0)->sprite_frame_index == 795);
    REQUIRE(table.at(1)->flags == 60);
    REQUIRE(table.at(1)->lifetime == 96);
    REQUIRE(table.at(1)->speed == 4000);
    REQUIRE(table.at(1)->trail_red == 255);
    REQUIRE(table.at(1)->trail_green == 128);
    REQUIRE(table.at(1)->trail_blue == 20);
    REQUIRE(table.at(2) == nullptr);
}

TEST_CASE("object table rejects malformed input", "[object_table]") {
    ObjectTable table;
    REQUIRE(ObjectTable::parse(std::vector<std::byte>(16), table) == ObjectTableError::TooSmall);
    REQUIRE(ObjectTable::parse(std::vector<std::byte>(80, std::byte{0xAB}), table) ==
            ObjectTableError::NotCompressed);
    REQUIRE(ObjectTable::parse(make_entry({{"item", 1}}, true), table) ==
            ObjectTableError::BadCount);
}
