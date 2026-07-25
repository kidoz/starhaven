#ifndef STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
#define STARHAVEN_CORE_WORLD_MAP_EVENT_HPP

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
constexpr std::size_t kChestRecordSize = 4204;
constexpr std::size_t kOutdoorEventTrailerSize = 256;

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
    std::uint32_t contained_item_id = 0;  // direct zero-based `ITEMS.TXT` id
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
constexpr std::size_t kContainedItemRecordSize = 28;
constexpr std::size_t kSpriteObjectPreviousPositionOffset = 0x58;

// Returns no records when the outdoor section layout is malformed.
[[nodiscard]] std::vector<MapSpriteObject> extract_sprite_objects(const MapEventFile& file,
                                                                  std::size_t max_records = 4096);

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
