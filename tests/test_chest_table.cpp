// Tests for DCHEST.BIN.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/world/chest_table.hpp"

using namespace starhaven::world;

namespace {

struct ChestSpec {
    std::string name;
    std::uint16_t frame_index = 0x0a0e;
    std::uint16_t object_id = 0;
};

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::byte> make_entry(const std::vector<ChestSpec>& chests, bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + chests.size() * kChestRecordSize, 0);
    const auto count =
        static_cast<std::uint32_t>(corrupt_count ? chests.size() + 1 : chests.size());
    for (int i = 0; i < 4; ++i) {
        raw[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((count >> (8 * i)) & 0xFF);
    }

    for (std::size_t i = 0; i < chests.size(); ++i) {
        const ChestSpec& chest = chests[i];
        const std::size_t base = 4 + i * kChestRecordSize;
        for (std::size_t k = 0; k < chest.name.size() && k < kChestNameSize; ++k) {
            raw[base + k] = static_cast<std::uint8_t>(chest.name[k]);
        }
        put_u16(raw, base + 0x20, chest.frame_index);
        put_u16(raw, base + 0x22, chest.object_id);
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

TEST_CASE("chest appearances decode the name to object id join", "[chest_table]") {
    const ChestSpec wooden{.name = "wooden chest", .object_id = 1};
    const ChestSpec sack{.name = "sack", .object_id = 3};
    const ChestSpec metal{.name = "metal chest", .object_id = 8};

    ChestTable table;
    REQUIRE(ChestTable::parse(make_entry({wooden, sack, metal}), table) == ChestTableError::None);
    REQUIRE(table.size() == 3);
    REQUIRE(table.at(0)->name == "wooden chest");
    REQUIRE(table.at(0)->frame_index == 0x0a0e);
    REQUIRE(table.at(0)->object_id == 1);
    REQUIRE(table.at(1)->name == "sack");
    REQUIRE(table.at(1)->object_id == 3);
    REQUIRE(table.at(2)->name == "metal chest");
    REQUIRE(table.at(2)->object_id == 8);
    REQUIRE(table.at(3) == nullptr);
}

TEST_CASE("chest table rejects malformed input", "[chest_table]") {
    ChestTable table;
    REQUIRE(ChestTable::parse(std::vector<std::byte>(16), table) == ChestTableError::TooSmall);
    REQUIRE(ChestTable::parse(std::vector<std::byte>(80, std::byte{0xAB}), table) ==
            ChestTableError::NotCompressed);
    REQUIRE(ChestTable::parse(make_entry({{"chest", 1}}, true), table) == ChestTableError::BadCount);
}
