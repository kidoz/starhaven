#ifndef OPENMM6_CORE_IMAGE_BITMAP_HPP
#define OPENMM6_CORE_IMAGE_BITMAP_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openmm6::image {

// A decoded paletted image from a .LOD entry, expanded to 8-bit RGBA.
struct Bitmap {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    // width * height * 4 bytes, row-major top-to-bottom, RGBA premultiplied
    // only in the sense that transparent pixels have alpha 0.
    std::vector<std::uint8_t> rgba;
};

// Outcome of decoding. Callers convert these into user-facing text; the decoder
// never throws.
enum class BitmapError {
    None,
    // Input too small to hold the 48-byte header.
    TooSmall,
    // size field does not equal width*height, or width/height is zero.
    BadDimensions,
    // The pixel data region (dataSize) does not fit in the input.
    BadDataSize,
    // The 768-byte palette is missing or truncated.
    PaletteTruncated,
    // decompressedSize is non-zero but the total length is inconsistent.
    BadDecompressedSize,
    // zlib inflate failed or produced the wrong number of bytes.
    InflateFailed,
};

// Decode a .LOD image entry's raw bytes into an RGBA bitmap. Handles both
// uncompressed and zlib-compressed pixel data. Only the base mipmap (the first
// width*height pixels) is decoded; mipmaps are ignored.
[[nodiscard]] BitmapError decode_bitmap(std::span<const std::byte> entry,
                                        Bitmap& out);

// The fixed sizes from the verified spec, exposed for tests and tools.
constexpr std::uint32_t kHeaderSize = 48;
constexpr std::uint32_t kPaletteSize = 768;  // 256 * 3

}  // namespace openmm6::image

#endif  // OPENMM6_CORE_IMAGE_BITMAP_HPP
