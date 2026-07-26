#include "core/world/map_event.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace starhaven::world {

using image::detail::inflate_all;

namespace {

bool section_end(std::size_t start, std::uint32_t count, std::size_t stride, std::size_t limit,
                 std::size_t& end) {
    if (count > (std::numeric_limits<std::size_t>::max() - start) / stride) {
        return false;
    }
    end = start + static_cast<std::size_t>(count) * stride;
    return end <= limit;
}

std::uint16_t u16_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::int16_t i16_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int16_t>(u16_at(bytes, offset));
}

std::int32_t i32_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(u32_at(bytes, offset));
}

MapItemInstance item_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    MapItemInstance item;
    item.item_id = i32_at(bytes, offset);
    item.standard_bonus_or_potion_power = i32_at(bytes, offset + 4);
    item.standard_bonus_strength = i32_at(bytes, offset + 8);
    item.special_bonus_or_gold_amount = i32_at(bytes, offset + 12);
    item.charges = i32_at(bytes, offset + 16);
    item.flags = u32_at(bytes, offset + 20);
    item.equipped_slot = bytes[offset + 24];
    for (std::size_t i = 0; i < item.reserved.size(); ++i) {
        item.reserved[i] = bytes[offset + 25 + i];
    }
    return item;
}

}  // namespace

MapEventError parse_map_event(std::span<const std::byte> entry, MapEventFile& out) {
    out = MapEventFile{};

    if (entry.size() < kEventWrapperSize) {
        return MapEventError::TooSmall;
    }

    io::ByteReader r{entry};
    [[maybe_unused]] const std::uint32_t stream_size = r.read_u32_le();
    const std::uint32_t decompressed_size = r.read_u32_le();

    if (!inflate_all(entry.subspan(kEventWrapperSize), out.payload)) {
        return MapEventError::InflateFailed;
    }
    if (decompressed_size != 0 && out.payload.size() != decompressed_size) {
        return MapEventError::SizeMismatch;
    }
    return MapEventError::None;
}

OutdoorEventLayoutError parse_outdoor_event_layout(const MapEventFile& file,
                                                   OutdoorEventLayout& out) {
    out = OutdoorEventLayout{};
    const auto& p = file.payload;
    if (p.size() < kOutdoorActorArrayOffset) {
        return OutdoorEventLayoutError::TooSmall;
    }

    out.actor_count = u32_at(p, kOutdoorActorCountOffset);
    out.actors_offset = kOutdoorActorArrayOffset;

    std::size_t cursor = 0;
    if (!section_end(out.actors_offset, out.actor_count, kActorRecordSize, p.size(), cursor) ||
        p.size() - cursor < 4) {
        return OutdoorEventLayoutError::BadSectionSize;
    }

    out.sprite_object_count = u32_at(p, cursor);
    out.sprite_objects_offset = cursor + 4;
    if (!section_end(out.sprite_objects_offset, out.sprite_object_count, kSpriteObjectRecordSize,
                     p.size(), cursor) ||
        p.size() - cursor < 4) {
        return OutdoorEventLayoutError::BadSectionSize;
    }

    out.chest_count = u32_at(p, cursor);
    out.chests_offset = cursor + 4;
    if (!section_end(out.chests_offset, out.chest_count, kChestRecordSize, p.size(), cursor)) {
        return OutdoorEventLayoutError::BadSectionSize;
    }

    out.trailer_offset = cursor;
    if (p.size() - cursor != kOutdoorEventTrailerSize) {
        return OutdoorEventLayoutError::BadTrailerSize;
    }
    return OutdoorEventLayoutError::None;
}

EventLayoutError parse_event_layout(const MapEventFile& file, EventLayout& out) {
    out = EventLayout{};
    const auto& p = file.payload;

    // Outdoor first: its trailer check is exact, so it cannot claim an indoor
    // payload, while the indoor chain would happily read three zero counts out
    // of an outdoor one.
    OutdoorEventLayout outdoor;
    if (parse_outdoor_event_layout(file, outdoor) == OutdoorEventLayoutError::None) {
        out.kind = MapEventKind::Outdoor;
        out.actor_count = outdoor.actor_count;
        out.actors_offset = outdoor.actors_offset;
        out.sprite_object_count = outdoor.sprite_object_count;
        out.sprite_objects_offset = outdoor.sprite_objects_offset;
        out.chest_count = outdoor.chest_count;
        out.chests_offset = outdoor.chests_offset;
        out.tail_offset = outdoor.trailer_offset;
        out.tail_size = kOutdoorEventTrailerSize;
        return EventLayoutError::None;
    }

    if (p.size() < kIndoorActorArrayOffset) {
        return EventLayoutError::TooSmall;
    }

    out.actor_count = u32_at(p, kIndoorActorCountOffset);
    out.actors_offset = kIndoorActorArrayOffset;

    std::size_t cursor = 0;
    if (!section_end(out.actors_offset, out.actor_count, kActorRecordSize, p.size(), cursor) ||
        p.size() - cursor < 4) {
        return EventLayoutError::BadSectionSize;
    }

    out.sprite_object_count = u32_at(p, cursor);
    out.sprite_objects_offset = cursor + 4;
    if (!section_end(out.sprite_objects_offset, out.sprite_object_count, kSpriteObjectRecordSize,
                     p.size(), cursor) ||
        p.size() - cursor < 4) {
        return EventLayoutError::BadSectionSize;
    }

    out.chest_count = u32_at(p, cursor);
    out.chests_offset = cursor + 4;
    if (!section_end(out.chests_offset, out.chest_count, kChestRecordSize, p.size(), cursor)) {
        return EventLayoutError::BadSectionSize;
    }

    // What follows is saved runtime state, not a counted section: it still
    // holds the stale heap pointers the original process wrote. Its size is
    // therefore recorded, not required to be anything.
    out.kind = MapEventKind::Indoor;
    out.tail_offset = cursor;
    out.tail_size = p.size() - cursor;
    return EventLayoutError::None;
}

std::vector<MapActor> extract_actors(const MapEventFile& file, std::size_t max_records) {
    EventLayout layout;
    if (parse_event_layout(file, layout) != EventLayoutError::None) {
        return {};
    }

    std::vector<MapActor> out;
    const std::size_t count = std::min<std::size_t>(layout.actor_count, max_records);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = layout.actors_offset + i * kActorRecordSize;
        MapActor actor;
        for (std::size_t k = 0; k < kActorNameSize; ++k) {
            const std::uint8_t ch = file.payload[base + kActorNameOffset + k];
            if (ch == 0) {
                break;
            }
            actor.name.push_back(static_cast<char>(ch));
        }
        actor.monster_id = file.payload[base + kActorMonsterIdOffset];
        actor.variant = file.payload[base + kActorVariantOffset];
        actor.x = i16_at(file.payload, base + kActorPositionOffset);
        actor.y = i16_at(file.payload, base + kActorPositionOffset + 2);
        actor.z = i16_at(file.payload, base + kActorPositionOffset + 4);
        out.push_back(std::move(actor));
    }
    return out;
}

std::vector<MapSpriteObject> extract_sprite_objects(const MapEventFile& file,
                                                    std::size_t max_records) {
    EventLayout layout;
    if (parse_event_layout(file, layout) != EventLayoutError::None) {
        return {};
    }

    std::vector<MapSpriteObject> out;
    const std::size_t count = std::min<std::size_t>(layout.sprite_object_count, max_records);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = layout.sprite_objects_offset + i * kSpriteObjectRecordSize;
        MapSpriteObject object;
        object.object_id = u16_at(file.payload, base + kSpriteObjectIdOffset);
        object.descriptor_index = u16_at(file.payload, base + kSpriteObjectDescriptorOffset);
        object.x = i32_at(file.payload, base + kSpriteObjectPositionOffset);
        object.y = i32_at(file.payload, base + kSpriteObjectPositionOffset + 4);
        object.z = i32_at(file.payload, base + kSpriteObjectPositionOffset + 8);
        object.velocity_x = i16_at(file.payload, base + kSpriteObjectVelocityOffset);
        object.velocity_y = i16_at(file.payload, base + kSpriteObjectVelocityOffset + 2);
        object.velocity_z = i16_at(file.payload, base + kSpriteObjectVelocityOffset + 4);
        object.facing = u16_at(file.payload, base + kSpriteObjectFacingOffset);
        object.attributes = u16_at(file.payload, base + kSpriteObjectAttributesOffset);
        object.sprite_frame = u16_at(file.payload, base + kSpriteObjectFrameOffset);
        object.contained_item = item_at(file.payload, base + kSpriteObjectItemOffset);
        object.previous_x = i32_at(file.payload, base + kSpriteObjectPreviousPositionOffset);
        object.previous_y = i32_at(file.payload, base + kSpriteObjectPreviousPositionOffset + 4);
        object.previous_z = i32_at(file.payload, base + kSpriteObjectPreviousPositionOffset + 8);
        out.push_back(object);
    }
    return out;
}

std::vector<MapChestItem> extract_chest_items(const MapEventFile& file, std::size_t max_records) {
    EventLayout layout;
    if (parse_event_layout(file, layout) != EventLayoutError::None) {
        return {};
    }

    std::vector<MapChestItem> out;
    out.reserve(std::min<std::size_t>(
        static_cast<std::size_t>(layout.chest_count) * kChestItemCount, max_records));
    for (std::size_t chest = 0; chest < layout.chest_count; ++chest) {
        const std::size_t chest_base = layout.chests_offset + chest * kChestRecordSize;
        for (std::size_t slot = 0; slot < kChestItemCount; ++slot) {
            const std::size_t offset =
                chest_base + kChestItemArrayOffset + slot * kContainedItemRecordSize;
            const MapItemInstance item = item_at(file.payload, offset);
            if (item.empty()) {
                continue;
            }
            if (out.size() == max_records) {
                return out;
            }
            out.push_back({chest, slot, item});
        }
    }
    return out;
}

}  // namespace starhaven::world
