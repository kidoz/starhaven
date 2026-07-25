// Tests for the .ddm/.dlv event-data parser.
//
// Fixtures are SYNTHETIC: a known payload is zlib-compressed and wrapped per
// docs/formats/event-data.md. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

#include <zlib.h>

#include "core/world/map_event.hpp"

using namespace starhaven::world;

namespace {

bool zlib_compress(const std::vector<std::uint8_t>& src,
                   std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()),
                  static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

std::vector<std::byte> make_event_entry(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kEventWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kEventWrapperSize], compressed.data(), compressed.size());
    return entry;
}

}  // namespace

TEST_CASE("valid event payload decompresses to the original bytes", "[map_event]") {
    std::vector<std::uint8_t> payload(100, 0);
    payload[10] = 0xAB; payload[11] = 0xCD;
    auto entry = make_event_entry(payload);
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    REQUIRE(f.payload.size() == 100);
    REQUIRE(f.payload[10] == 0xAB);
    REQUIRE(f.payload[11] == 0xCD);
}

TEST_CASE("truncated wrapper is rejected", "[map_event]") {
    std::vector<std::byte> entry(4, std::byte{0});
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::TooSmall);
}

TEST_CASE("corrupt zlib is rejected", "[map_event]") {
    std::vector<std::byte> entry(kEventWrapperSize + 16, std::byte{0xFF});
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::InflateFailed);
}

TEST_CASE("decompressed-size mismatch is rejected", "[map_event]") {
    auto entry = make_event_entry(std::vector<std::uint8_t>(50, 0));
    put_u32_le(entry, 0x04, 999999);  // lie about decompressed size
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::SizeMismatch);
}

TEST_CASE("decompressed size of zero is accepted (unknown)", "[map_event]") {
    auto entry = make_event_entry(std::vector<std::uint8_t>(50, 0));
    put_u32_le(entry, 0x04, 0);
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    REQUIRE(f.payload.size() == 50);
}

// --- event-table tests -----------------------------------------------------

// Build a decompressed payload large enough for `populated` records at the
// event-table offset, stamping type/name into each.
std::vector<std::byte> make_event_entry_with_table(
    const std::vector<std::pair<std::int32_t, std::string>>& records) {
    std::vector<std::uint8_t> payload(kEventTableOffset + records.size() * kEventRecordSize + 16, 0);
    for (std::size_t i = 0; i < records.size(); ++i) {
        const std::size_t rec = kEventTableOffset + i * kEventRecordSize;
        const auto [type, name] = records[i];
        payload[rec] = static_cast<std::uint8_t>(type & 0xFF);
        payload[rec + 1] = static_cast<std::uint8_t>((type >> 8) & 0xFF);
        payload[rec + 2] = static_cast<std::uint8_t>((type >> 16) & 0xFF);
        payload[rec + 3] = static_cast<std::uint8_t>((type >> 24) & 0xFF);
        for (std::size_t c = 0; c < name.size() && c < kEventRecordNameMax; ++c) {
            payload[rec + kEventRecordNameOffset + c] = static_cast<std::uint8_t>(name[c]);
        }
    }
    return make_event_entry(payload);
}

TEST_CASE("populated event records are enumerated with type and name", "[map_event]") {
    auto entry = make_event_entry_with_table({{20, "Peasant"}, {0, "Guard"}, {7, "Merchant"}});
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    auto recs = enumerate_event_table(f);
    REQUIRE(recs.size() == 3);
    REQUIRE(recs[0].type == 20); REQUIRE(recs[0].name == "Peasant");
    REQUIRE(recs[1].type == 0);  REQUIRE(recs[1].name == "Guard");   // populated via name
    REQUIRE(recs[2].type == 7);  REQUIRE(recs[2].name == "Merchant");
}

TEST_CASE("empty event table yields no records", "[map_event]") {
    // Payload sized for the table region but all zero.
    std::vector<std::uint8_t> payload(kEventTableOffset + 3 * kEventRecordSize, 0);
    auto entry = make_event_entry(payload);
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    REQUIRE(enumerate_event_table(f).empty());
}

TEST_CASE("event table stops safely on a truncated payload", "[map_event]") {
    // Payload that ends mid-table (only enough for part of record 0).
    std::vector<std::uint8_t> payload(kEventTableOffset + 4, 0);
    payload[kEventTableOffset] = 1;  // type=1
    auto entry = make_event_entry(payload);
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    // Not enough bytes for even one record's name field -> no records.
    REQUIRE(enumerate_event_table(f).empty());
}

TEST_CASE("empty slots between populated records are skipped", "[map_event]") {
    auto entry = make_event_entry_with_table({{5, "A"}, {0, ""}, {0, ""}, {9, "D"}});
    MapEventFile f;
    REQUIRE(parse_map_event(entry, f) == MapEventError::None);
    auto recs = enumerate_event_table(f);
    REQUIRE(recs.size() == 2);
    REQUIRE(recs[0].name == "A");
    REQUIRE(recs[1].name == "D");
}

// --- actor tests -----------------------------------------------------------

namespace {

// Build an event payload whose actor table holds the given entries.
std::vector<std::byte> make_event_entry_with_actors(
    const std::vector<std::tuple<std::string, std::int16_t, std::int16_t,
                                 std::int16_t>>& actors,
    bool trailing_garbage = false) {
    const std::size_t count = actors.size() + (trailing_garbage ? 1 : 0);
    std::vector<std::uint8_t> payload(
        kEventTableOffset + (count + 1) * kEventRecordSize, 0);

    auto put_i16 = [&](std::size_t off, std::int16_t v) {
        const auto u = static_cast<std::uint16_t>(v);
        payload[off] = static_cast<std::uint8_t>(u & 0xFF);
        payload[off + 1] = static_cast<std::uint8_t>((u >> 8) & 0xFF);
    };

    for (std::size_t i = 0; i < actors.size(); ++i) {
        const auto& [name, x, y, z] = actors[i];
        const std::size_t base = kEventTableOffset + i * kEventRecordSize;
        for (std::size_t k = 0; k < name.size() && k < 31; ++k) {
            payload[base + kEventRecordNameOffset + k] =
                static_cast<std::uint8_t>(name[k]);
        }
        put_i16(base + kActorPositionOffset, x);
        put_i16(base + kActorPositionOffset + 2, y);
        put_i16(base + kActorPositionOffset + 4, z);
    }

    if (trailing_garbage) {
        // Several shipped files end with a slot holding a one-character name
        // and an implausible position; it must not be reported as an actor.
        const std::size_t base = kEventTableOffset + actors.size() * kEventRecordSize;
        payload[base + kEventRecordNameOffset] = static_cast<std::uint8_t>('(');
        put_i16(base + kActorPositionOffset, 0);
        put_i16(base + kActorPositionOffset + 2, 0);
        put_i16(base + kActorPositionOffset + 4, 31744);
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kEventWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kEventWrapperSize], compressed.data(), compressed.size());
    return entry;
}

}  // namespace

TEST_CASE("actors decode with their names and positions", "[map_event]") {
    auto entry = make_event_entry_with_actors({
        {"Peasant", 10896, 15872, 160},
        {"Peasant", -13632, 18976, 96},
        {"Goblin", 0, -2048, -64},
    });
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);

    const auto actors = extract_actors(file);
    REQUIRE(actors.size() == 3);
    REQUIRE(actors[0].name == "Peasant");
    REQUIRE(actors[0].x == 10896);
    REQUIRE(actors[0].y == 15872);
    REQUIRE(actors[0].z == 160);
    REQUIRE(actors[1].x == -13632);   // negative coordinates survive
    REQUIRE(actors[2].name == "Goblin");
    REQUIRE(actors[2].z == -64);
}

TEST_CASE("a trailing one-character slot ends the actor array", "[map_event]") {
    auto entry = make_event_entry_with_actors({{"Peasant", 100, 200, 300}},
                                              /*trailing_garbage*/ true);
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    const auto actors = extract_actors(file);
    REQUIRE(actors.size() == 1);
    REQUIRE(actors[0].name == "Peasant");
}

TEST_CASE("a map with no actors yields none", "[map_event]") {
    auto entry = make_event_entry_with_actors({});
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    REQUIRE(extract_actors(file).empty());
}

TEST_CASE("a payload too short for the table yields no actors", "[map_event]") {
    auto entry = make_event_entry(std::vector<std::uint8_t>(64, 0));
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    REQUIRE(extract_actors(file).empty());
}

TEST_CASE("a non-printable name ends the array", "[map_event]") {
    auto entry = make_event_entry_with_actors({{"Peasant", 1, 2, 3}});
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    // Corrupt the second slot's name with a control byte.
    const std::size_t second = kEventTableOffset + kEventRecordSize;
    file.payload[second + kEventRecordNameOffset] = 0x01;
    file.payload[second + kEventRecordNameOffset + 1] = 0x02;
    REQUIRE(extract_actors(file).size() == 1);
}

TEST_CASE("actors carry the monster id that indexes DMONLIST", "[map_event]") {
    auto entry = make_event_entry_with_actors({{"Peasant", 1, 2, 3}});
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    // Stamp a monster id into the first record.
    file.payload[kEventTableOffset + kActorMonsterIdOffset] = 133;
    const auto actors = extract_actors(file);
    REQUIRE(actors.size() == 1);
    REQUIRE(actors[0].monster_id == 133);
}
