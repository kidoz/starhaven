#ifndef STARHAVEN_TOOLS_WALKER_SPRITES_HPP
#define STARHAVEN_TOOLS_WALKER_SPRITES_HPP

// Turning a name into the sprite to draw this frame. Decorations and monster
// animations both name an entry in the sprite frame table rather than a
// picture, so the table decides which SPRITES.LOD entry is current and how
// large to draw it. See docs/formats/dsft.md.

#include <cstdint>
#include <string>

#include "core/assets/asset_cache.hpp"
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

}  // namespace starhaven::tools

#endif  // STARHAVEN_TOOLS_WALKER_SPRITES_HPP
