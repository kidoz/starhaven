#ifndef OPENMM6_CORE_RENDER_TILE_SET_HPP
#define OPENMM6_CORE_RENDER_TILE_SET_HPP

#include <array>
#include <cstdint>

#include "core/render/texture.hpp"

namespace openmm6::render {

// Ground textures indexed by the tilemap value stored in an .odm map.
//
// This type is the single seam between "which tile does this cell use" and
// "what pixels does that tile have". Resolving a real MM6 tile index to its
// ground bitmap requires the engine's tileset tables, which are not yet
// decoded (see docs/rendering/terrain-coloring.md). Until they are, callers
// build a placeholder set; when the real loader lands it only has to produce a
// TileSet, and nothing downstream changes.
class TileSet {
public:
    // A tilemap index is one byte, so the set is a dense 256-entry table.
    static constexpr std::size_t kSlotCount = 256;

    TileSet() = default;

    // Generated stand-in textures, NOT game art.
    //
    // Each slot gets a flat-color tile derived from tile_type_color(), with a
    // slightly darkened checker so that texture coordinates are visibly
    // exercised: a wrong UV shows up as a warped or missing checker rather
    // than hiding inside a uniform color.
    //
    // `tile_px` is the edge length of each generated tile; values below 2 are
    // raised to 2 so the checker remains visible.
    [[nodiscard]] static TileSet make_placeholder(int tile_px = 16);

    // Install a texture for one tile index. Returns false if `texture` is
    // empty, so a failed decode cannot silently register a hole.
    bool set(std::uint8_t index, Texture texture);

    // The texture for a tile index. Always returns a valid reference; an
    // unresolved slot yields an empty texture, which draw_triangle_textured
    // treats as "skip". Callers therefore never need a null check.
    [[nodiscard]] const Texture& texture_for(std::uint8_t index) const noexcept;

    // True when no slot has been filled.
    [[nodiscard]] bool empty() const noexcept { return filled_ == 0; }

    // How many slots hold a texture.
    [[nodiscard]] std::size_t size() const noexcept { return filled_; }

private:
    std::array<Texture, kSlotCount> tiles_{};
    std::size_t filled_ = 0;
};

}  // namespace openmm6::render

#endif  // OPENMM6_CORE_RENDER_TILE_SET_HPP
