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

// --- Indoor DLV layout (see docs/formats/event-tables.md) ------------------

// Indoor files carry the same three counted sections in the same order, after
// a shorter and — unlike the outdoor one — unaligned fixed prefix.
constexpr std::size_t kIndoorActorCountOffset = 883;
constexpr std::size_t kIndoorActorArrayOffset = 887;

// Immediately after the chest array, every indoor payload carries a fixed
// 200-entry array of 80-byte records — not a counted section. Each record's
// fields at +0x24..+0x40 are eight pointers into the writing process's heap.
constexpr std::size_t kIndoorStateSlotCount = 200;
constexpr std::size_t kIndoorStateSlotSize = 80;
constexpr std::size_t kIndoorStateBlockSize = kIndoorStateSlotCount * kIndoorStateSlotSize;

enum class MapEventKind : std::uint8_t {
    Unknown,
    Outdoor,
    Indoor,
};

// The counted sections of either kind of event file.
struct EventLayout {
    MapEventKind kind = MapEventKind::Unknown;
    std::uint32_t actor_count = 0;
    std::size_t actors_offset = 0;
    std::uint32_t sprite_object_count = 0;
    std::size_t sprite_objects_offset = 0;
    std::uint32_t chest_count = 0;
    std::size_t chests_offset = 0;

    // What follows the chest array: outdoor's fixed 256-byte trailer, or the
    // indoor block of saved runtime state.
    std::size_t tail_offset = 0;
    std::size_t tail_size = 0;

    // Indoor only: the fixed 200-slot record array at the start of the tail.
    std::size_t state_offset = 0;
    std::size_t state_size = 0;
};

enum class EventLayoutError : std::uint8_t {
    None,
    // The payload cannot hold either layout's fixed prefix.
    TooSmall,
    // A count-times-stride section runs past the payload.
    BadSectionSize,
    // An indoor payload has no room for its fixed state block and trailer.
    BadStateBlock,
};

// Decode either layout, choosing between them by trying the outdoor one first.
//
// The order is not arbitrary. An outdoor payload also chains from the indoor
// offset, but yields three zero counts there, so testing indoor first would
// silently report an empty outdoor map. The outdoor test cannot misfire the
// other way: it demands an exact 256-byte trailer, which no indoor payload
// satisfies.
[[nodiscard]] EventLayoutError parse_event_layout(const MapEventFile& file, EventLayout& out);

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

    // Chest item ids -1..-6 request deferred generation in placeholder classes
    // 1..6. The map's treasure class later resolves the final treasure level.
    // Returns zero for a concrete item id or an unknown negative value.
    [[nodiscard]] int random_treasure_class() const noexcept {
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

// --- Doors (see docs/formats/event-tables.md) ------------------------------

// One door: which vertices slide, which way, how far, and the faces those
// vertices touch. The indoor event file's fixed 200-slot block holds the
// 80-byte records, and the region after it holds each door's id arrays in
// slot order — their total is exactly the state size the paired `.blv`
// declares.
struct MapDoor {
    std::uint32_t attributes = 0;
    std::uint32_t id = 0;                  // what opcode 15 throws
    float dx = 0, dy = 0, dz = 0;          // unit direction, from 16.16
    int distance = 0;                      // how far it moves, world units
    int open_speed = 0, close_speed = 0;
    std::vector<std::uint16_t> vertex_ids;  // the vertices that move
    std::vector<std::uint16_t> face_ids;    // the faces those vertices touch
    std::vector<std::uint16_t> sector_ids;
    std::vector<std::int16_t> delta_us, delta_vs;    // per-face texture slide
    std::vector<std::int16_t> x_base, y_base, z_base;  // the shut position

    // Live state, not from the file: every base equals its shipped vertex on
    // 4,067 of 4,067 across the 52 maps, so a door starts shut. `progress`
    // is how far along its travel the door stands, 0 shut to 1 open, so a
    // thrown lever slides the geometry instead of teleporting it.
    bool open = false;
    float progress = 0.0f;
};

// Decode the door block of an indoor event file. Returns no records for an
// outdoor file, and stops at the first slot whose arrays would run past the
// region the paired level declared.
[[nodiscard]] std::vector<MapDoor> extract_doors(const MapEventFile& file);

struct MapChestItem {
    std::size_t chest_index = 0;
    std::size_t slot_index = 0;
    MapItemInstance item;
};

// Each chest's first u16: 0..7 across every shipped file, the row of
// `DCHEST.BIN` whose own last field numbers the CHEST01..CHEST08 art —
// the chest's appearance.
[[nodiscard]] std::vector<std::uint16_t> extract_chest_appearances(const MapEventFile& file);

// The u16 beside the appearance: the chest's flags word. Bit 0 is the trap,
// set on 1,191 of the 1,340 shipped chests; bit 1 is the runtime "items
// placed" mark and ships zero. See docs/formats/event-tables.md.
[[nodiscard]] std::vector<std::uint16_t> extract_chest_flags(const MapEventFile& file);

// Return the nonempty item slots across the decoded chest array. Concrete item
// ids and negative random-generation placeholders are both retained.
[[nodiscard]] std::vector<MapChestItem> extract_chest_items(const MapEventFile& file,
                                                            std::size_t max_records = 4096);

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
