#ifndef STARHAVEN_TOOLS_WALKER_SPRITES_HPP
#define STARHAVEN_TOOLS_WALKER_SPRITES_HPP

// Turning a name into the sprite to draw this frame. Decorations and monster
// animations both name an entry in the sprite frame table rather than a
// picture, so the table decides which SPRITES.LOD entry is current and how
// large to draw it. See docs/formats/dsft.md.

#include <cstdint>
#include <string>

#include "core/assets/asset_cache.hpp"
#include "core/world/monster_list.hpp"
#include "core/world/object_table.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace starhaven::tools {

// The frame table counts animation time in units it does not name. Fifteen per
// second puts a torch's ten-tick flicker at two thirds of a second and an
// archer's eighteen-tick walk at a little over one, which is a calibration by
// eye rather than a fact from the data. `inferred`
inline constexpr double kSpriteTicksPerSecond = 15.0;

inline std::uint32_t sprite_ticks(std::uint64_t elapsed_ms) {
    return static_cast<std::uint32_t>(static_cast<double>(elapsed_ms) * kSpriteTicksPerSecond /
                                      1000.0);
}

// What to draw for `name` right now.
struct SpriteChoice {
    std::string entry;   // a SPRITES.LOD entry name
    float scale = 1.0f;  // the frame table's size multiplier
    // The palette to draw it through, or kSpritePaletteFromHeader. A monster's
    // B and C variants share the A variant's picture and differ only here.
    int palette = assets::kSpritePaletteFromHeader;
};

// Resolve through the frame table when the name is an animation, and fall back
// to treating it as a sprite name when it is not — 81 of the 230 decoration
// types have no group, and those are still drawable.
inline SpriteChoice choose_sprite(const world::SpriteFrameTable& frames, const std::string& name,
                                  std::uint32_t ticks, int view = 0) {
    if (const world::SpriteFrame* f = frames.frame_at(name, ticks)) {
        return {world::SpriteFrameTable::sprite_entry(*f, view), f->scale_factor(),
                static_cast<int>(f->palette_id)};
    }
    return {name, 1.0f, assets::kSpritePaletteFromHeader};
}

// The animation to draw for an actor, or an empty string when nothing
// resolves.
//
// An actor record's monster id is **1-based**: it is the id in `MONSTERS.TXT`,
// which is the `DMONLIST.BIN` index plus one. Across the 266 outdoor actors
// the record's own name matches `MONSTERS.TXT[id]` 266 times and the binary
// table's name matches at `id - 1` for all 173 monsters, so indexing
// `DMONLIST` with the raw id draws the next monster's sprite.
inline std::string actor_animation(const world::MonsterList& monsters,
                                   const world::SpriteFrameTable& frames, assets::AssetCache& cache,
                                   int monster_id) {
    auto drawable = [&](const std::string& animation) {
        if (animation.empty())
            return false;
        const auto group = frames.group(animation);
        if (group.empty())
            return cache.has_sprite(animation);
        return cache.has_sprite(world::SpriteFrameTable::sprite_entry(group.front(), 0));
    };
    if (monster_id <= 0) {
        return {};
    }
    const auto index = static_cast<std::size_t>(monster_id - 1);
    const auto* entry = monsters.at(index);
    if (entry == nullptr) {
        return {};
    }
    std::string animation = entry->animation(world::MonsterAnimation::Stand);
    // Monsters come in A/B/C triples; with 1-based ids the A variant of id is
    // id - ((id - 1) % 3). Falling back to it draws a variant whose own art is
    // missing rather than drawing nothing.
    if (!drawable(animation)) {
        const auto a_index = static_cast<std::size_t>(monster_id - 1 - ((monster_id - 1) % 3));
        if (const auto* a = monsters.at(a_index); a != nullptr) {
            const std::string& alt = a->animation(world::MonsterAnimation::Stand);
            if (drawable(alt))
                return alt;
        }
    }
    return drawable(animation) ? animation : std::string{};
}

// What to draw for a placed sprite object. Its descriptor names the first
// frame of an animation group rather than a picture.
inline SpriteChoice choose_object_sprite(const world::SpriteFrameTable& frames,
                                         const world::ObjectDescriptor& descriptor,
                                         std::uint32_t ticks) {
    const std::size_t index = descriptor.sprite_frame_index;
    if (index >= frames.size() || frames.frames()[index].group_name.empty()) {
        return {};
    }
    return choose_sprite(frames, frames.frames()[index].group_name, ticks);
}

}  // namespace starhaven::tools

#endif  // STARHAVEN_TOOLS_WALKER_SPRITES_HPP
