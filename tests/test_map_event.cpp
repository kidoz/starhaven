// Tests for the .ddm/.dlv event-data parser.
//
// Fixtures are synthetic. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/world/map_event.hpp"

using namespace starhaven::world;

namespace {

bool zlib_compress(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()), static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

void put_u16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    v[off] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>(x >> 8);
}

void put_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF);
    }
}

void put_i16(std::vector<std::uint8_t>& v, std::size_t off, std::int16_t x) {
    put_u16(v, off, static_cast<std::uint16_t>(x));
}

void put_i32(std::vector<std::uint8_t>& v, std::size_t off, std::int32_t x) {
    put_u32(v, off, static_cast<std::uint32_t>(x));
}

void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

std::vector<std::byte> make_event_entry(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kEventWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(compressed.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kEventWrapperSize], compressed.data(), compressed.size());
    return entry;
}

struct ActorSpec {
    std::string name;
    std::uint8_t monster_id = 0;
    std::uint8_t variant = 0;
    std::int16_t x = 0, y = 0, z = 0;
};

struct ObjectSpec {
    std::uint16_t object_id = 0;
    std::uint16_t descriptor_index = 0;
    std::int32_t x = 0, y = 0, z = 0;
    std::int16_t velocity_x = 0, velocity_y = 0, velocity_z = 0;
    std::uint16_t facing = 0;
    std::uint16_t attributes = 0;
    std::uint16_t sprite_frame = 0;
    std::int32_t item_id = 0;
    std::int32_t standard_bonus_or_potion_power = 0;
    std::int32_t standard_bonus_strength = 0;
    std::int32_t special_bonus_or_gold_amount = 0;
    std::int32_t charges = 0;
    std::uint32_t item_flags = 0;
    std::uint8_t equipped_slot = 0;
    std::int32_t previous_x = 0, previous_y = 0, previous_z = 0;
};

std::vector<std::uint8_t>
make_outdoor_payload(const std::vector<ActorSpec>& actors, const std::vector<ObjectSpec>& objects,
                     std::uint32_t chest_count = 2,
                     std::size_t trailer_size = kOutdoorEventTrailerSize) {
    const std::size_t objects_offset =
        kOutdoorActorArrayOffset + actors.size() * kActorRecordSize + 4;
    const std::size_t chests_offset = objects_offset + objects.size() * kSpriteObjectRecordSize + 4;
    std::vector<std::uint8_t> payload(chests_offset + chest_count * kChestRecordSize + trailer_size,
                                      0);

    put_u32(payload, kOutdoorActorCountOffset, static_cast<std::uint32_t>(actors.size()));
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const std::size_t base = kOutdoorActorArrayOffset + i * kActorRecordSize;
        for (std::size_t k = 0; k < actors[i].name.size() && k < kActorNameSize; ++k) {
            payload[base + k] = static_cast<std::uint8_t>(actors[i].name[k]);
        }
        payload[base + kActorMonsterIdOffset] = actors[i].monster_id;
        payload[base + kActorVariantOffset] = actors[i].variant;
        put_i16(payload, base + kActorPositionOffset, actors[i].x);
        put_i16(payload, base + kActorPositionOffset + 2, actors[i].y);
        put_i16(payload, base + kActorPositionOffset + 4, actors[i].z);
    }

    put_u32(payload, objects_offset - 4, static_cast<std::uint32_t>(objects.size()));
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const ObjectSpec& object = objects[i];
        const std::size_t base = objects_offset + i * kSpriteObjectRecordSize;
        put_u16(payload, base + kSpriteObjectIdOffset, object.object_id);
        put_u16(payload, base + kSpriteObjectDescriptorOffset, object.descriptor_index);
        put_i32(payload, base + kSpriteObjectPositionOffset, object.x);
        put_i32(payload, base + kSpriteObjectPositionOffset + 4, object.y);
        put_i32(payload, base + kSpriteObjectPositionOffset + 8, object.z);
        put_i16(payload, base + kSpriteObjectVelocityOffset, object.velocity_x);
        put_i16(payload, base + kSpriteObjectVelocityOffset + 2, object.velocity_y);
        put_i16(payload, base + kSpriteObjectVelocityOffset + 4, object.velocity_z);
        put_u16(payload, base + kSpriteObjectFacingOffset, object.facing);
        put_u16(payload, base + kSpriteObjectAttributesOffset, object.attributes);
        put_u16(payload, base + kSpriteObjectFrameOffset, object.sprite_frame);
        put_i32(payload, base + kSpriteObjectItemOffset, object.item_id);
        put_i32(payload, base + kSpriteObjectItemOffset + 4, object.standard_bonus_or_potion_power);
        put_i32(payload, base + kSpriteObjectItemOffset + 8, object.standard_bonus_strength);
        put_i32(payload, base + kSpriteObjectItemOffset + 12, object.special_bonus_or_gold_amount);
        put_i32(payload, base + kSpriteObjectItemOffset + 16, object.charges);
        put_u32(payload, base + kSpriteObjectItemOffset + 20, object.item_flags);
        payload[base + kSpriteObjectItemOffset + 24] = object.equipped_slot;
        put_i32(payload, base + kSpriteObjectPreviousPositionOffset, object.previous_x);
        put_i32(payload, base + kSpriteObjectPreviousPositionOffset + 4, object.previous_y);
        put_i32(payload, base + kSpriteObjectPreviousPositionOffset + 8, object.previous_z);
    }

    put_u32(payload, chests_offset - 4, chest_count);
    return payload;
}

}  // namespace

TEST_CASE("valid event payload decompresses to the original bytes", "[map_event]") {
    std::vector<std::uint8_t> payload(100, 0);
    payload[10] = 0xAB;
    payload[11] = 0xCD;
    auto entry = make_event_entry(payload);
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    REQUIRE(file.payload == payload);
}

TEST_CASE("event wrapper failures are deterministic", "[map_event]") {
    MapEventFile file;
    REQUIRE(parse_map_event(std::vector<std::byte>(4), file) == MapEventError::TooSmall);

    std::vector<std::byte> corrupt(kEventWrapperSize + 16, std::byte{0xFF});
    REQUIRE(parse_map_event(corrupt, file) == MapEventError::InflateFailed);

    auto mismatch = make_event_entry(std::vector<std::uint8_t>(50, 0));
    put_u32_le(mismatch, 0x04, 999999);
    REQUIRE(parse_map_event(mismatch, file) == MapEventError::SizeMismatch);
}

TEST_CASE("a zero declared decompressed size means unknown", "[map_event]") {
    auto entry = make_event_entry(std::vector<std::uint8_t>(50, 0));
    put_u32_le(entry, 0x04, 0);
    MapEventFile file;
    REQUIRE(parse_map_event(entry, file) == MapEventError::None);
    REQUIRE(file.payload.size() == 50);
}

TEST_CASE("outdoor section counts close on the fixed trailer", "[map_event]") {
    MapEventFile file{make_outdoor_payload({{"Peasant"}, {"Guard"}}, {{76, 54}}, 20)};
    OutdoorEventLayout layout;
    REQUIRE(parse_outdoor_event_layout(file, layout) == OutdoorEventLayoutError::None);
    REQUIRE(layout.actor_count == 2);
    REQUIRE(layout.actors_offset == kOutdoorActorArrayOffset);
    REQUIRE(layout.sprite_object_count == 1);
    REQUIRE(layout.sprite_objects_offset == kOutdoorActorArrayOffset + 2 * kActorRecordSize + 4);
    REQUIRE(layout.chest_count == 20);
    REQUIRE(file.payload.size() - layout.trailer_offset == kOutdoorEventTrailerSize);
}

TEST_CASE("outdoor layout rejects truncation and corrupt counts", "[map_event]") {
    OutdoorEventLayout layout;
    MapEventFile too_small{std::vector<std::uint8_t>(64, 0)};
    REQUIRE(parse_outdoor_event_layout(too_small, layout) == OutdoorEventLayoutError::TooSmall);

    MapEventFile bad_actor_count{make_outdoor_payload({}, {})};
    put_u32(bad_actor_count.payload, kOutdoorActorCountOffset, 0xFFFFFFFFu);
    REQUIRE(parse_outdoor_event_layout(bad_actor_count, layout) ==
            OutdoorEventLayoutError::BadSectionSize);

    MapEventFile bad_object_count{make_outdoor_payload({}, {})};
    put_u32(bad_object_count.payload, kOutdoorActorArrayOffset, 0xFFFFFFFFu);
    REQUIRE(parse_outdoor_event_layout(bad_object_count, layout) ==
            OutdoorEventLayoutError::BadSectionSize);

    MapEventFile short_trailer{make_outdoor_payload({}, {}, 2, 255)};
    REQUIRE(parse_outdoor_event_layout(short_trailer, layout) ==
            OutdoorEventLayoutError::BadTrailerSize);
}

TEST_CASE("actors use the counted array and corrected field offsets", "[map_event]") {
    MapEventFile file{make_outdoor_payload(
        {
            {"Peasant", 133, 2, 10896, 15872, 160},
            {"Goblin", 17, 3, -13632, 18976, -64},
        },
        {})};
    const auto actors = extract_actors(file);
    REQUIRE(actors.size() == 2);
    REQUIRE(actors[0].name == "Peasant");
    REQUIRE(actors[0].monster_id == 133);
    REQUIRE(actors[0].variant == 2);
    REQUIRE(actors[0].x == 10896);
    REQUIRE(actors[0].y == 15872);
    REQUIRE(actors[0].z == 160);
    REQUIRE(actors[1].name == "Goblin");
    REQUIRE(actors[1].x == -13632);
    REQUIRE(actors[1].z == -64);
}

TEST_CASE("actor extraction honors the caller limit", "[map_event]") {
    MapEventFile file{make_outdoor_payload({{"One"}, {"Two"}, {"Three"}}, /*objects*/ {})};
    REQUIRE(extract_actors(file, 2).size() == 2);
}

TEST_CASE("sprite objects decode identity, motion, item, and previous position", "[map_event]") {
    const ObjectSpec placed{
        .object_id = 76,
        .descriptor_index = 54,
        .x = 123456,
        .y = -654321,
        .z = 96,
        .velocity_x = 120,
        .velocity_y = -40,
        .velocity_z = 8,
        .facing = 1536,
        .attributes = 0x7C00,
        .sprite_frame = 7,
        .item_id = 160,
        .standard_bonus_or_potion_power = 4,
        .standard_bonus_strength = 12,
        .special_bonus_or_gold_amount = 9,
        .charges = 6,
        .item_flags = kItemFlagIdentified | kItemFlagBroken,
        .equipped_slot = 3,
        .previous_x = 123000,
        .previous_y = -654000,
        .previous_z = 80,
    };
    MapEventFile file{make_outdoor_payload({}, {placed})};
    const auto objects = extract_sprite_objects(file);
    REQUIRE(objects.size() == 1);
    REQUIRE(objects[0].object_id == 76);
    REQUIRE(objects[0].descriptor_index == 54);
    REQUIRE(objects[0].x == 123456);
    REQUIRE(objects[0].y == -654321);
    REQUIRE(objects[0].z == 96);
    REQUIRE(objects[0].velocity_x == 120);
    REQUIRE(objects[0].velocity_y == -40);
    REQUIRE(objects[0].velocity_z == 8);
    REQUIRE(objects[0].facing == 1536);
    REQUIRE(objects[0].attributes == 0x7C00);
    REQUIRE(objects[0].sprite_frame == 7);
    REQUIRE(objects[0].contained_item.item_id == 160);
    REQUIRE(objects[0].contained_item.standard_bonus_or_potion_power == 4);
    REQUIRE(objects[0].contained_item.standard_bonus_strength == 12);
    REQUIRE(objects[0].contained_item.special_bonus_or_gold_amount == 9);
    REQUIRE(objects[0].contained_item.charges == 6);
    REQUIRE(objects[0].contained_item.identified());
    REQUIRE(objects[0].contained_item.broken());
    REQUIRE(objects[0].contained_item.equipped_slot == 3);
    REQUIRE(objects[0].previous_x == 123000);
    REQUIRE(objects[0].previous_y == -654000);
    REQUIRE(objects[0].previous_z == 80);
}

TEST_CASE("chests expose fixed items and random treasure placeholders", "[map_event]") {
    MapEventFile file{make_outdoor_payload({}, {}, 2)};
    OutdoorEventLayout layout;
    REQUIRE(parse_outdoor_event_layout(file, layout) == OutdoorEventLayoutError::None);

    const std::size_t fixed = layout.chests_offset + kChestItemArrayOffset;
    put_i32(file.payload, fixed, 42);
    put_i32(file.payload, fixed + 4, 2);
    put_i32(file.payload, fixed + 8, 5);
    put_i32(file.payload, fixed + 12, 7);
    put_i32(file.payload, fixed + 16, 11);
    put_u32(file.payload, fixed + 20, kItemFlagIdentified);
    file.payload[fixed + 24] = 9;

    const std::size_t deferred = layout.chests_offset + kChestRecordSize + kChestItemArrayOffset +
                                 17 * kContainedItemRecordSize;
    put_i32(file.payload, deferred, -6);

    const auto items = extract_chest_items(file);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].chest_index == 0);
    REQUIRE(items[0].slot_index == 0);
    REQUIRE(items[0].item.item_id == 42);
    REQUIRE(items[0].item.standard_bonus_or_potion_power == 2);
    REQUIRE(items[0].item.standard_bonus_strength == 5);
    REQUIRE(items[0].item.special_bonus_or_gold_amount == 7);
    REQUIRE(items[0].item.charges == 11);
    REQUIRE(items[0].item.identified());
    REQUIRE(items[0].item.equipped_slot == 9);
    REQUIRE(items[0].item.random_treasure_level() == 0);

    REQUIRE(items[1].chest_index == 1);
    REQUIRE(items[1].slot_index == 17);
    REQUIRE(items[1].item.item_id == -6);
    REQUIRE(items[1].item.random_treasure_level() == 6);
    REQUIRE(extract_chest_items(file, 1).size() == 1);
}

TEST_CASE("object extraction rejects malformed layout and honors its limit", "[map_event]") {
    MapEventFile file{make_outdoor_payload({}, {{1, 1}, {2, 2}, {3, 3}})};
    REQUIRE(extract_sprite_objects(file, 2).size() == 2);
    file.payload.pop_back();
    REQUIRE(extract_sprite_objects(file).empty());
}
