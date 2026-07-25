#include "core/image/bitmap.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <array>
#include <cstddef>
#include <cstring>

namespace openmm6::image {

using detail::inflate_all;

BitmapError decode_bitmap(std::span<const std::byte> entry, Bitmap& out) {
    out = Bitmap{};

    if (entry.size() < kHeaderSize) {
        return BitmapError::TooSmall;
    }

    io::ByteReader r{entry};
    r.seek(0x10);
    const std::uint32_t size = r.read_u32_le();
    const std::uint32_t data_size = r.read_u32_le();
    const std::uint16_t width = r.read_u16_le();
    const std::uint16_t height = r.read_u16_le();
    // Skip widthLn2/heightLn2/widthMinus1/heightMinus1/paletteId/anotherPaletteId.
    r.seek(0x28);
    const std::uint32_t decompressed_size = r.read_u32_le();
    const std::uint32_t flags = r.read_u32_le();

    if (width == 0 || height == 0) {
        return BitmapError::BadDimensions;
    }
    if (static_cast<std::uint64_t>(width) * height != size) {
        return BitmapError::BadDimensions;
    }

    // The pixel data region starts right after the 48-byte header.
    if (static_cast<std::uint64_t>(kHeaderSize) + data_size > entry.size()) {
        return BitmapError::BadDataSize;
    }
    // The 768-byte palette follows the pixel data.
    if (static_cast<std::uint64_t>(kHeaderSize) + data_size + kPaletteSize >
        entry.size()) {
        return BitmapError::PaletteTruncated;
    }

    const std::span<const std::byte> pixel_block =
        entry.subspan(kHeaderSize, data_size);

    // Obtain the decompressed palette indices for the base mipmap.
    std::vector<std::uint8_t> pixels;
    if (decompressed_size == 0) {
        // Uncompressed: the pixel block must hold at least `size` indices.
        if (data_size < size) {
            return BitmapError::BadDataSize;
        }
        pixels.assign(reinterpret_cast<const std::uint8_t*>(pixel_block.data()),
                      reinterpret_cast<const std::uint8_t*>(pixel_block.data()) +
                          size);
    } else {
        if (!inflate_all(pixel_block, pixels)) {
            return BitmapError::InflateFailed;
        }
        if (pixels.size() < size) {
            return BitmapError::BadDecompressedSize;
        }
    }

    // Read the palette (256 RGB entries).
    std::span<const std::byte> palette_span =
        entry.subspan(kHeaderSize + data_size, kPaletteSize);
    std::array<std::uint8_t, kPaletteSize> palette{};
    std::memcpy(palette.data(), palette_span.data(), kPaletteSize);

    const bool index0_transparent = (flags & 0x0200) != 0;

    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(size) * 4, 0);
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t idx = pixels[i];
        const std::size_t p = static_cast<std::size_t>(idx) * 3;
        std::uint8_t* px = &out.rgba[i * 4];
        px[0] = palette[p + 0];  // R
        px[1] = palette[p + 1];  // G
        px[2] = palette[p + 2];  // B
        px[3] = (index0_transparent && idx == 0) ? 0 : 255;
    }

    return BitmapError::None;
}

}  // namespace openmm6::image
