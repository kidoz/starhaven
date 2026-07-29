// Tests for the DMONLIST.BIN monster table.
//
// Fixtures are SYNTHETIC: the container is assembled here from the layout in
// docs/formats/dmonlist.md. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/world/monster_list.hpp"

using namespace starhaven::world;

namespace {

struct MonsterSpec {
    std::string name;
    std::vector<std::string> animations;
    std::array<std::uint16_t, 4> sounds{};
    std::uint16_t height = 0;
    std::uint16_t radius = 0;
};

// Build the stored entry: a 48-byte header then a zlib stream holding a u32
// count and that many 148-byte records.
std::vector<std::byte> make_entry(const std::vector<MonsterSpec>& monsters,
                                  bool corrupt_count = false) {
    std::vector<std::uint8_t> raw(4 + monsters.size() * kMonsterRecordSize, 0);
    const auto count =
        static_cast<std::uint32_t>(corrupt_count ? monsters.size() + 3 : monsters.size());
    for (int i = 0; i < 4; ++i) {
        raw[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((count >> (8 * i)) & 0xFF);
    }
    for (std::size_t i = 0; i < monsters.size(); ++i) {
        const std::size_t base = 4 + i * kMonsterRecordSize;
        const auto& m = monsters[i];
        const auto put_u16 = [&raw](std::size_t at, std::uint16_t value) {
            raw[at] = static_cast<std::uint8_t>(value & 0xFF);
            raw[at + 1] = static_cast<std::uint8_t>(value >> 8);
        };
        put_u16(base + kMonsterHeightOffset, m.height);
        put_u16(base + kMonsterRadiusOffset, m.radius);
        for (std::size_t k = 0; k < m.sounds.size(); ++k) {
            put_u16(base + kMonsterSoundOffset + k * 2, m.sounds[k]);
        }
        for (std::size_t k = 0; k < m.name.size() && k < kMonsterNameSize - 1; ++k) {
            raw[base + kMonsterNameOffset + k] = static_cast<std::uint8_t>(m.name[k]);
        }
        for (std::size_t a = 0; a < m.animations.size() && a < kMonsterAnimationCount; ++a) {
            const std::size_t at = base + kMonsterAnimationOffset + a * kMonsterAnimationNameSize;
            const auto& s = m.animations[a];
            for (std::size_t k = 0; k < s.size() && k < kMonsterAnimationNameSize - 1; ++k) {
                raw[at + k] = static_cast<std::uint8_t>(s[k]);
            }
        }
    }

    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> packed(bound);
    uLongf len = bound;
    REQUIRE(compress2(packed.data(), &len, raw.data(), static_cast<uLong>(raw.size()),
                      Z_DEFAULT_COMPRESSION) == Z_OK);
    packed.resize(len);

    std::vector<std::byte> entry(48 + packed.size(), std::byte{0});
    std::memcpy(&entry[48], packed.data(), packed.size());
    return entry;
}

}  // namespace

TEST_CASE("monster records decode names and animation sprites", "[monster_list]") {
    auto entry = make_entry({
        {"ArcherA", {"arc1sta", "arc1wka", "arc1atk"}, {1010, 1011, 1012, 1014}, 161, 40},
        {"PeasantF1A", {"pfemsta", "pfemwaa"}},
    });
    MonsterList list;
    REQUIRE(MonsterList::parse(entry, list) == MonsterListError::None);
    REQUIRE(list.size() == 2);
    REQUIRE(list.entries()[0].name == "ArcherA");
    REQUIRE(list.entries()[0].animation(MonsterAnimation::Stand) == "arc1sta");
    REQUIRE(list.entries()[0].animation(MonsterAnimation::Walk) == "arc1wka");
    REQUIRE(list.entries()[1].name == "PeasantF1A");
    REQUIRE(list.entries()[1].animation(MonsterAnimation::Stand) == "pfemsta");
    // Unset animation slots come back empty rather than as garbage.
    REQUIRE(list.entries()[1].animation(MonsterAnimation::Fidget).empty());
    // The four sound ids are stated outright — the Guards' fidget skips
    // one, so a base+offset reading would be wrong — and the body sizes
    // ride at the record's front. A record without them reads zero.
    REQUIRE(list.entries()[0].sounds == std::array<std::uint16_t, 4>{1010, 1011, 1012, 1014});
    REQUIRE(list.entries()[0].height == 161);
    REQUIRE(list.entries()[0].radius == 40);
    REQUIRE(list.entries()[1].sounds == std::array<std::uint16_t, 4>{0, 0, 0, 0});
}

TEST_CASE("an id past the end resolves to nullptr, not garbage", "[monster_list]") {
    auto entry = make_entry({{"ArcherA", {"arc1sta"}}});
    MonsterList list;
    REQUIRE(MonsterList::parse(entry, list) == MonsterListError::None);
    REQUIRE(list.at(0) != nullptr);
    REQUIRE(list.at(1) == nullptr);
    REQUIRE(list.at(9999) == nullptr);
}

TEST_CASE("a count disagreeing with the block length is rejected", "[monster_list]") {
    // The record size and count must account for the whole block; otherwise
    // every field would be read at the wrong offset.
    auto entry = make_entry({{"ArcherA", {"arc1sta"}}}, /*corrupt_count*/ true);
    MonsterList list;
    REQUIRE(MonsterList::parse(entry, list) == MonsterListError::BadCount);
}

TEST_CASE("an entry too small for the header is rejected", "[monster_list]") {
    const std::vector<std::byte> tiny(16, std::byte{0});
    MonsterList list;
    REQUIRE(MonsterList::parse(tiny, list) == MonsterListError::TooSmall);
}

TEST_CASE("a non-zlib body is rejected", "[monster_list]") {
    std::vector<std::byte> entry(128, std::byte{0xAB});
    MonsterList list;
    REQUIRE(MonsterList::parse(entry, list) == MonsterListError::NotCompressed);
}
