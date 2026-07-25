#ifndef STARHAVEN_CORE_RENDER_TEXTURE_HPP
#define STARHAVEN_CORE_RENDER_TEXTURE_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "core/render/color.hpp"

namespace starhaven::render {

// How texture coordinates outside [0,1) are resolved.
//
// Byte-sized because a wrap mode is stored per surface, and there will be one
// per facet once model and terrain texturing land.
enum class WrapMode : std::uint8_t {
    // Tile the texture. MM6 ground tiles repeat across terrain cells, and
    // building facets repeat a wall texture along their length.
    Repeat,
    // Clamp to the edge texel. Required for atlas cells and decoded sprites,
    // where repeating would bleed a neighbouring image across the seam.
    Clamp,
};

// An 8-bit RGBA texture in top-to-bottom row order — the same layout the .LOD
// image decoders produce (see core/image/bitmap.hpp and core/image/sprite.hpp),
// so a decoded bitmap can be handed here without a conversion pass.
class Texture {
public:
    Texture() = default;

    // Build a texture from decoded RGBA bytes. `rgba` must be exactly
    // width*height*4 bytes and both dimensions must be positive.
    //
    // Returns false and leaves `out` empty on any mismatch. Texture data comes
    // from parsed archive entries, so a malformed or truncated entry must fail
    // here rather than produce an object whose sampler reads out of bounds.
    [[nodiscard]] static bool create(int width, int height,
                                     std::vector<std::uint8_t> rgba, Texture& out);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] bool empty() const noexcept { return rgba_.empty(); }
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return rgba_; }

    // Fetch a texel by integer coordinate, resolved through `wrap`.
    // Out-of-range coordinates are always brought in range; this never reads
    // outside the buffer. Returns transparent black for an empty texture.
    [[nodiscard]] Color texel(int x, int y, WrapMode wrap) const noexcept;

    // Nearest-neighbour sample at normalized coordinates.
    //
    // MM6 point-samples its textures; bilinear filtering would be an
    // enhancement rather than fidelity, so nearest is the default and only
    // mode here. v runs downward to match the row order above: v=0 is the top
    // row, v=1 the bottom.
    //
    // Non-finite u or v (which a degenerate projection can produce) resolve to
    // texel (0,0) rather than an out-of-range read.
    [[nodiscard]] Color sample(float u, float v, WrapMode wrap) const noexcept;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> rgba_;
};

}  // namespace starhaven::render

#endif  // STARHAVEN_CORE_RENDER_TEXTURE_HPP
