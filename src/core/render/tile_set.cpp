#include "core/render/tile_set.hpp"

#include <algorithm>
#include <vector>

#include "core/render/terrain_mesh.hpp"

namespace starhaven::render {

namespace {

std::uint8_t to_byte(float linear) {
    return static_cast<std::uint8_t>(std::clamp(linear, 0.0f, 1.0f) * 255.0f);
}

}  // namespace

bool TileSet::set(std::uint8_t index, Texture texture) {
    if (texture.empty()) {
        return false;
    }
    if (tiles_[index].empty()) {
        ++filled_;
    }
    tiles_[index] = std::move(texture);
    return true;
}

const Texture& TileSet::texture_for(std::uint8_t index) const noexcept {
    return tiles_[index];
}

TileSet TileSet::make_placeholder(int tile_px) {
    const int extent = std::max(2, tile_px);
    const int half = extent / 2;

    TileSet set;
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        const auto index = static_cast<std::uint8_t>(i);
        const Vec3Color base = tile_type_color(index);

        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(extent) * extent * 4U);
        for (int y = 0; y < extent; ++y) {
            for (int x = 0; x < extent; ++x) {
                // Two-tone checker: the darker square makes UV errors visible.
                const bool dark = ((x < half) != (y < half));
                const float k = dark ? 0.72f : 1.0f;
                const std::size_t p =
                    (static_cast<std::size_t>(y) * extent + x) * 4U;
                rgba[p + 0] = to_byte(base.r * k);
                rgba[p + 1] = to_byte(base.g * k);
                rgba[p + 2] = to_byte(base.b * k);
                rgba[p + 3] = 255;
            }
        }

        Texture t;
        if (Texture::create(extent, extent, std::move(rgba), t)) {
            set.set(index, std::move(t));
        }
    }
    return set;
}

}  // namespace starhaven::render
