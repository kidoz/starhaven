// Tests for the ground-tile texture table.
//
// Hermetic: the placeholder generator produces its own pixels, and the manual
// cases below build synthetic textures. Nothing here reads a game install.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/render/texture.hpp"
#include "core/render/tile_set.hpp"

using namespace starhaven::render;

namespace {

// Catch2 cannot decompose a chained comparison inside REQUIRE, so equality
// over all four channels goes through a helper rather than `a == b && ...`.
bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

Texture solid(int extent, Color c) {
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(extent) * extent * 4U);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = c.r;
        rgba[i + 1] = c.g;
        rgba[i + 2] = c.b;
        rgba[i + 3] = c.a;
    }
    Texture t;
    REQUIRE(Texture::create(extent, extent, std::move(rgba), t));
    return t;
}

}  // namespace

TEST_CASE("a default tile set is empty and every slot is unresolved",
          "[tile_set]") {
    const TileSet set;
    REQUIRE(set.empty());
    REQUIRE(set.size() == 0);
    // An unresolved slot must still return a usable reference; the rasterizer
    // treats an empty texture as "skip", so callers never need a null check.
    REQUIRE(set.texture_for(0).empty());
    REQUIRE(set.texture_for(255).empty());
}

TEST_CASE("set installs a texture and reports occupancy", "[tile_set]") {
    TileSet set;
    REQUIRE(set.set(90, solid(4, Color{10, 20, 30, 255})));
    REQUIRE_FALSE(set.empty());
    REQUIRE(set.size() == 1);
    REQUIRE_FALSE(set.texture_for(90).empty());
    REQUIRE(set.texture_for(91).empty());
}

TEST_CASE("set rejects an empty texture rather than registering a hole",
          "[tile_set]") {
    TileSet set;
    REQUIRE_FALSE(set.set(5, Texture{}));
    REQUIRE(set.empty());
    REQUIRE(set.size() == 0);
}

TEST_CASE("replacing an existing slot does not double-count it", "[tile_set]") {
    TileSet set;
    REQUIRE(set.set(7, solid(2, Color{1, 1, 1, 255})));
    REQUIRE(set.set(7, solid(2, Color{9, 9, 9, 255})));
    REQUIRE(set.size() == 1);
    // The second install must win.
    REQUIRE(set.texture_for(7).texel(0, 0, WrapMode::Clamp).r == 9);
}

TEST_CASE("the placeholder set fills every tile index", "[tile_set]") {
    const TileSet set = TileSet::make_placeholder(8);
    REQUIRE(set.size() == TileSet::kSlotCount);
    for (int i = 0; i < 256; ++i) {
        const Texture& t = set.texture_for(static_cast<std::uint8_t>(i));
        REQUIRE_FALSE(t.empty());
        REQUIRE(t.width() == 8);
        REQUIRE(t.height() == 8);
    }
}

TEST_CASE("placeholder tiles are opaque and carry a visible checker",
          "[tile_set]") {
    const TileSet set = TileSet::make_placeholder(8);
    const Texture& t = set.texture_for(90);  // the common grass base tile

    // Opaque everywhere: terrain must not become see-through.
    REQUIRE(t.texel(0, 0, WrapMode::Clamp).a == 255);
    REQUIRE(t.texel(7, 7, WrapMode::Clamp).a == 255);

    // Diagonally opposite quadrants share a tone; adjacent ones differ. This
    // is what makes a wrong UV show up as a warped checker rather than hiding
    // inside a flat color.
    const Color top_left = t.texel(1, 1, WrapMode::Clamp);
    const Color top_right = t.texel(6, 1, WrapMode::Clamp);
    const Color bottom_right = t.texel(6, 6, WrapMode::Clamp);
    REQUIRE(top_left.g != top_right.g);
    REQUIRE(top_left.g == bottom_right.g);
}

TEST_CASE("distinct tile indices get distinguishable placeholder colors",
          "[tile_set]") {
    const TileSet set = TileSet::make_placeholder(4);
    // Grass (90) and water (>=180) are deliberately different families.
    const Color grass = set.texture_for(90).texel(0, 0, WrapMode::Clamp);
    const Color water = set.texture_for(190).texel(0, 0, WrapMode::Clamp);
    REQUIRE_FALSE(same(grass, water));
    // Water should read blue-dominant, grass green-dominant.
    REQUIRE(water.b > water.r);
    REQUIRE(grass.g > grass.r);
}

TEST_CASE("a degenerate tile size is raised to a usable extent", "[tile_set]") {
    // A checker needs at least 2x2; smaller requests must not produce a
    // zero-sized texture that Texture::create would reject.
    const TileSet set = TileSet::make_placeholder(0);
    REQUIRE(set.size() == TileSet::kSlotCount);
    REQUIRE(set.texture_for(0).width() >= 2);
}
