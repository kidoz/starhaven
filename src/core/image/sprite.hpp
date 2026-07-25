#ifndef OPENMM6_CORE_IMAGE_SPRITE_HPP
#define OPENMM6_CORE_IMAGE_SPRITE_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "core/image/palette.hpp"

namespace openmm6::image {

// A decoded sprite: dimensions, the palette it references, and the per-scanline
// visible spans expressed as an RGBA buffer (transparent outside the spans).
struct Sprite {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t palette_id = 0;
    // width * height * 4 bytes, row-major top-to-bottom, RGBA. Pixels outside
    // any line's [begin, end) span are fully transparent (alpha 0). Index 0
    // inside a span is also transparent.
    std::vector<std::uint8_t> rgba;
};

// Outcome of decoding. Callers convert these into user-facing text.
enum class SpriteError {
    None,
    TooSmall,             // fewer than 32 bytes for the header
    BadDimensions,        // width or height is zero
    LineTableTruncated,   // 32 + height*8 exceeds the buffer
    BadDataSize,          // dataSize inconsistent with total size
    PixelDataTruncated,   // the pixel region is shorter than dataSize
    InflateFailed,        // zlib failure or wrong decompressed length
    LineOutOfBounds,      // a line's offset + span exceeds the pixel buffer
};

// Header and line-table constants from the verified spec, exposed for tests.
constexpr std::uint32_t kSpriteHeaderSize = 32;
constexpr std::uint32_t kSpriteLineSize = 8;

// One per-scanline record, as read from the line table.
struct SpriteLine {
    std::int16_t begin = 0;
    std::int16_t end = 0;
    std::uint32_t offset = 0;
};

// Decode a .LOD sprite entry's raw bytes into an RGBA sprite. The sprite's own
// palette indices are mapped through `palette`. The caller is responsible for
// loading the correct `palXXX` palette (see palette_entry_name / the sprite's
// palette_id).
[[nodiscard]] SpriteError decode_sprite(std::span<const std::byte> entry,
                                        const Palette& palette,
                                        Sprite& out);

// Read just the header + line table without applying a palette. Useful for
// validation and for tools that want to report dimensions before loading a
// palette. Fills width, height, palette_id, and the line records; does not
// touch rgba.
struct SpriteHeader {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t palette_id = 0;
    std::uint32_t data_size = 0;
    std::uint32_t decompressed_size = 0;
    std::vector<SpriteLine> lines;
};
[[nodiscard]] SpriteError read_sprite_header(std::span<const std::byte> entry,
                                             SpriteHeader& out);

}  // namespace openmm6::image

#endif  // OPENMM6_CORE_IMAGE_SPRITE_HPP
