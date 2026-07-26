#ifndef STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
#define STARHAVEN_CORE_WORLD_MAP_EVENT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// A decompressed map event-data file (.ddm outdoor / .dlv indoor).
struct MapEventFile {
    std::vector<std::uint8_t> payload;
};

enum class MapEventError : std::uint8_t {
    None,
    TooSmall,
    InflateFailed,
    SizeMismatch,
};

// Parse a raw .ddm/.dlv entry (as read from Games.lod).
[[nodiscard]] MapEventError parse_map_event(std::span<const std::byte> entry, MapEventFile& out);

constexpr std::uint32_t kEventWrapperSize = 8;

// --- Outdoor DDM layout (see docs/formats/event-tables.md) -----------------

constexpr std::size_t kOutdoorActorCountOffset = 0x798;
constexpr std::size_t kOutdoorActorArrayOffset = 0x79C;
constexpr std::size_t kActorRecordSize = 548;
constexpr std::size_t kSpriteObjectRecordSize = 100;
constexpr std::size_t kContainedItemRecordSize = 28;
constexpr std::size_t kChestRecordSize = 4204;
constexpr std::size_t kOutdoorEventTrailerSize = 256;
constexpr std::size_t kChestItemArrayOffset = 4;
constexpr std::size_t kChestItemCount = 140;
constexpr std::size_t kChestGridOffset =
    kChestItemArrayOffset + kChestItemCount * kContainedItemRecordSize;
static_assert(kChestGridOffset + kChestItemCount * sizeof(std::int16_t) == kChestRecordSize);

struct OutdoorEventLayout {
    std::uint32_t actor_count = 0;
    std::size_t actors_offset = 0;
    std::uint32_t sprite_object_count = 0;
    std::size_t sprite_objects_offset = 0;
    std::uint32_t chest_count = 0;
    std::size_t chests_offset = 0;
    std::size_t trailer_offset = 0;
};

enum class OutdoorEventLayoutError : std::uint8_t {
    None,
    TooSmall,
    BadSectionSize,
    BadTrailerSize,
};

// Decode the counted sections of an outdoor .ddm payload. Counts and strides
// must account for the complete payload, including its fixed 256-byte trailer.
[[nodiscard]] OutdoorEventLayoutError parse_outdoor_event_layout(const MapEventFile& file,
                                                                 OutdoorEventLayout& out);

// --- Actors (see docs/formats/event-actors.md) -----------------------------

struct MapActor {
    std::string name;
    std::uint8_t monster_id = 0;
    std::uint8_t variant = 0;
    std::int16_t x = 0, y = 0, z = 0;
};

constexpr std::size_t kActorNameOffset = 0x00;
constexpr std::size_t kActorNameSize = 32;
constexpr std::size_t kActorMonsterIdOffset = 0x34;
constexpr std::size_t kActorVariantOffset = 0x35;
constexpr std::size_t kActorPositionOffset = 0x7E;

// Returns no records when the outdoor section layout is malformed.
[[nodiscard]] std::vector<MapActor> extract_actors(const MapEventFile& file,
                                                   std::size_t max_records = 4096);

// --- Sprite objects (see docs/formats/event-objects.md) --------------------

constexpr std::uint32_t kItemFlagIdentified = 0x01;
constexpr std::uint32_t kItemFlagBroken = 0x02;

// The serialized item instance shared by sprite objects, chests, and
// inventories. Several fields are intentionally named for both meanings the
// original format overloads according to item type.
struct MapItemInstance {
    std::int32_t item_id = 0;
    std::int32_t standard_bonus_or_potion_power = 0;
    std::int32_t standard_bonus_strength = 0;
    std::int32_t special_bonus_or_gold_amount = 0;
    std::int32_t charges = 0;
    std::uint32_t flags = 0;
    std::uint8_t equipped_slot = 0;
    std::array<std::uint8_t, 3> reserved{};

    [[nodiscard]] bool empty() const noexcept { return item_id == 0; }
    [[nodiscard]] bool identified() const noexcept { return (flags & kItemFlagIdentified) != 0; }
    [[nodiscard]] bool broken() const noexcept { return (flags & kItemFlagBroken) != 0; }

    // Chest item ids -1..-6 request deferred generation at treasure levels
    // 1..6. Returns zero for a concrete item id or an unknown negative value.
    [[nodiscard]] int random_treasure_level() const noexcept {
        return item_id >= -6 && item_id <= -1 ? -item_id : 0;
    }
};

// One placed loot object or projectile. Fields whose meaning is not yet
// independently verified remain in the raw 100-byte record and are not
// exposed here.
struct MapSpriteObject {
    std::uint16_t object_id = 0;
    std::uint16_t descriptor_index = 0;
    std::int32_t x = 0, y = 0, z = 0;
    std::int16_t velocity_x = 0, velocity_y = 0, velocity_z = 0;
    std::uint16_t facing = 0;
    std::uint16_t attributes = 0;
    std::uint16_t sprite_frame = 0;
    MapItemInstance contained_item;
    std::int32_t previous_x = 0, previous_y = 0, previous_z = 0;
};

constexpr std::size_t kSpriteObjectIdOffset = 0x00;
constexpr std::size_t kSpriteObjectDescriptorOffset = 0x02;
constexpr std::size_t kSpriteObjectPositionOffset = 0x04;
constexpr std::size_t kSpriteObjectVelocityOffset = 0x10;
constexpr std::size_t kSpriteObjectFacingOffset = 0x16;
constexpr std::size_t kSpriteObjectAttributesOffset = 0x1A;
constexpr std::size_t kSpriteObjectFrameOffset = 0x1E;
constexpr std::size_t kSpriteObjectItemOffset = 0x24;
constexpr std::size_t kSpriteObjectPreviousPositionOffset = 0x58;

// Returns no records when the outdoor section layout is malformed.
[[nodiscard]] std::vector<MapSpriteObject> extract_sprite_objects(const MapEventFile& file,
                                                                  std::size_t max_records = 4096);

struct MapChestItem {
    std::size_t chest_index = 0;
    std::size_t slot_index = 0;
    MapItemInstance item;
};

// Return the nonempty item slots across the decoded chest array. Concrete item
// ids and negative random-generation placeholders are both retained.
[[nodiscard]] std::vector<MapChestItem> extract_chest_items(const MapEventFile& file,
                                                            std::size_t max_records = 4096);

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
