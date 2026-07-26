// Tests for the DSOUNDS.BIN sound table and the DDECLIST.BIN decoration table.
//
// Hermetic: every fixture is synthesized from the formats described in
// docs/formats/dsounds.md and docs/formats/odm-decorations.md. No bytes are
// copied from a game archive.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include "core/world/decoration_table.hpp"
#include "core/world/sound_table.hpp"

using namespace starhaven::world;

namespace {

void put_u16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    v[off] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>(x >> 8);
}

void put_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF);
    }
}

void put_name(std::vector<std::uint8_t>& v, std::size_t off, const std::string& s,
              std::size_t width) {
    for (std::size_t i = 0; i < s.size() && i < width; ++i) {
        v[off + i] = static_cast<std::uint8_t>(s[i]);
    }
}

// Wrap a decompressed table body as a stored archive entry.
std::vector<std::byte> wrap(const std::vector<std::uint8_t>& body, const char* name) {
    uLongf cap = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::uint8_t> z(cap);
    REQUIRE(compress(z.data(), &cap, body.data(), static_cast<uLong>(body.size())) == Z_OK);
    z.resize(cap);

    std::vector<std::byte> out(48, std::byte{0});
    std::memcpy(out.data(), name, std::strlen(name));
    for (const std::uint8_t b : z)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

struct SoundSpec {
    std::string name;
    std::uint32_t id;
    std::uint32_t group;
};

std::vector<std::byte> sound_entry(const std::vector<SoundSpec>& rows) {
    std::vector<std::uint8_t> body(4 + rows.size() * kSoundRecordSize, 0);
    put_u32(body, 0, static_cast<std::uint32_t>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t base = 4 + i * kSoundRecordSize;
        put_name(body, base, rows[i].name, kSoundNameSize);
        put_u32(body, base + 0x20, rows[i].id);
        put_u32(body, base + 0x24, rows[i].group);
    }
    return wrap(body, "dsounds.bin");
}

struct DecorationSpec {
    std::string name;
    std::string group;
    std::uint16_t sound_id;
};

std::vector<std::byte> decoration_entry(const std::vector<DecorationSpec>& rows) {
    std::vector<std::uint8_t> body(4 + rows.size() * kDecorationTableRecordSize, 0);
    put_u32(body, 0, static_cast<std::uint32_t>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t base = 4 + i * kDecorationTableRecordSize;
        put_name(body, base, rows[i].name, kDecorationTableNameSize);
        put_name(body, base + 0x20, rows[i].group, 32);
        put_u16(body, base + kDecorationTableSoundOffset, rows[i].sound_id);
    }
    return wrap(body, "ddeclist.bin");
}

}  // namespace

TEST_CASE("sound records carry a name, an id and a group", "[sound_table]") {
    const auto e = sound_entry({{"campfire", 4, 0}, {"enter", 6, 1}, {"fountain", 10, 0}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);
    REQUIRE(t.size() == 3);
    REQUIRE(t.entries()[0].name == "campfire");
    REQUIRE(t.entries()[0].id == 4);
    REQUIRE(t.entries()[1].group == 1);
}

TEST_CASE("sounds resolve by id and by name", "[sound_table]") {
    const auto e = sound_entry({{"campfire", 4, 0}, {"fountain", 10, 0}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);
    REQUIRE(t.find(10)->name == "fountain");
    REQUIRE(t.find(999) == nullptr);
    // The shipped tables disagree on case between one another.
    REQUIRE(t.find_by_name("CAMPFIRE")->id == 4);
    REQUIRE(t.find_by_name("nosuch") == nullptr);
}

TEST_CASE("a repeated sound id keeps the first record", "[sound_table]") {
    // Three ids are used twice in the shipped table; a lookup must be
    // deterministic rather than depend on map ordering.
    const auto e = sound_entry({{"first", 7, 0}, {"second", 7, 0}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);
    REQUIRE(t.size() == 2);
    REQUIRE(t.find(7)->name == "first");
}

TEST_CASE("an unnamed record is kept but not indexed by name", "[sound_table]") {
    const auto e = sound_entry({{"", 0, 1}, {"enter", 6, 1}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);
    REQUIRE(t.size() == 2);
    REQUIRE(t.find(0) != nullptr);
    REQUIRE(t.find_by_name("") == nullptr);
}

TEST_CASE("a sound table that does not close is rejected", "[sound_table]") {
    auto e = sound_entry({{"campfire", 4, 0}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);

    std::vector<std::uint8_t> body(4 + kSoundRecordSize, 0);
    put_u32(body, 0, 5);  // claim five records in a one-record block
    REQUIRE(SoundTable::parse(wrap(body, "dsounds.bin"), t) == SoundTableError::BadCount);

    const std::vector<std::byte> tiny(20, std::byte{0});
    REQUIRE(SoundTable::parse(tiny, t) == SoundTableError::TooSmall);

    const std::vector<std::byte> junk(60, std::byte{0});
    REQUIRE(SoundTable::parse(junk, t) == SoundTableError::NotCompressed);
}

TEST_CASE("decoration records carry an ambient sound id", "[decoration_table]") {
    const auto e = decoration_entry(
        {{"tree01", "tree", 0}, {"CampfireOn", "campfire", 4}, {"Statue", "statue", 10}});
    DecorationTable t;
    REQUIRE(DecorationTable::parse(e, t) == DecorationTableError::None);
    REQUIRE(t.size() == 3);
    REQUIRE(t.at(0)->name == "tree01");
    REQUIRE(t.at(0)->group == "tree");
    REQUIRE(t.at(0)->sound_id == 0);  // most decorations are silent
    REQUIRE(t.at(1)->sound_id == 4);
    REQUIRE(t.at(3) == nullptr);
}

TEST_CASE("decorations resolve by name for indoor maps", "[decoration_table]") {
    // Indoor decorations carry a name and no type id, so the name is the only
    // way into this table.
    const auto e = decoration_entry({{"CampfireOn", "campfire", 4}});
    DecorationTable t;
    REQUIRE(DecorationTable::parse(e, t) == DecorationTableError::None);
    REQUIRE(t.find("campfireon")->sound_id == 4);
    REQUIRE(t.find("CAMPFIREON")->sound_id == 4);
    REQUIRE(t.find("nosuch") == nullptr);
}

TEST_CASE("a decoration table that does not close is rejected", "[decoration_table]") {
    DecorationTable t;
    std::vector<std::uint8_t> body(4 + kDecorationTableRecordSize, 0);
    put_u32(body, 0, 9);
    REQUIRE(DecorationTable::parse(wrap(body, "ddeclist.bin"), t) ==
            DecorationTableError::BadCount);

    const std::vector<std::byte> tiny(20, std::byte{0});
    REQUIRE(DecorationTable::parse(tiny, t) == DecorationTableError::TooSmall);
}

TEST_CASE("monster sound ids are a block of ten per monster", "[sound_table]") {
    // 1000 + 10k + {attack, die, charge, fidget}; see docs/formats/dsounds.md.
    const auto e = sound_entry({{"01archerA_attack", 1000, 0},
                                {"01archerA_die", 1001, 0},
                                {"01archerA_charge", 1002, 0},
                                {"01archerA_fidget", 1003, 0},
                                {"04barbarianA_attack", 1010, 0}});
    SoundTable t;
    REQUIRE(SoundTable::parse(e, t) == SoundTableError::None);

    auto sound_of = [&](std::uint32_t block, MonsterSound action) {
        return t.find(kMonsterSoundBase + block * kMonsterSoundStride +
                      static_cast<std::uint32_t>(action));
    };
    REQUIRE(sound_of(0, MonsterSound::Attack)->name == "01archerA_attack");
    REQUIRE(sound_of(0, MonsterSound::Fidget)->name == "01archerA_fidget");
    REQUIRE(sound_of(1, MonsterSound::Attack)->name == "04barbarianA_attack");
    REQUIRE(sound_of(1, MonsterSound::Die) == nullptr);
}
