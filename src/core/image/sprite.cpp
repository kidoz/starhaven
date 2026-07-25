#include "core/image/sprite.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <cstddef>
#include <cstdint>

namespace openmm6::image {

using detail::inflate_all;

namespace {

// Header field offsets (see docs/formats/sprite.md).
constexpr std::uint32_t kNameSize = 12;
constexpr std::uint32_t kDataSizeOff = 0x0C;
constexpr std::uint32_t kWidthOff = 0x10;
constexpr std::uint32_t kHeightOff = 0x12;
constexpr std::uint32_t kPaletteIdOff = 0x14;
constexpr std::uint32_t kDecompressedOff = 0x1C;

}  // namespace

SpriteError read_sprite_header(std::span<const std::byte> entry,
                               SpriteHeader& out) {
    out = SpriteHeader{};

    if (entry.size() < kSpriteHeaderSize) {
        return SpriteError::TooSmall;
    }

    io::ByteReader r{entry};
    r.seek(kDataSizeOff);
    out.data_size = r.read_u32_le();
    r.seek(kWidthOff);
    out.width = r.read_u16_le();
    r.seek(kHeightOff);
    out.height = r.read_u16_le();
    r.seek(kPaletteIdOff);
    out.palette_id = r.read_u16_le();
    r.seek(kDecompressedOff);
    out.decompressed_size = r.read_u32_le();

    if (out.width == 0 || out.height == 0) {
        return SpriteError::BadDimensions;
    }

    // The line table must fit, and the total size must be consistent.
    const std::uint64_t line_table_end =
        static_cast<std::uint64_t>(kSpriteHeaderSize) +
        static_cast<std::uint64_t>(out.height) * kSpriteLineSize;
    if (line_table_end > entry.size()) {
        return SpriteError::LineTableTruncated;
    }
    if (line_table_end + out.data_size != entry.size()) {
        return SpriteError::BadDataSize;
    }

    out.lines.reserve(out.height);
    for (std::uint16_t y = 0; y < out.height; ++y) {
        const std::uint64_t off =
            static_cast<std::uint64_t>(kSpriteHeaderSize) +
            static_cast<std::uint64_t>(y) * kSpriteLineSize;
        r.seek(static_cast<std::size_t>(off));
        SpriteLine line;
        // Line record: begin(i16), end(i16), offset(u32) = 8 bytes total.
        line.begin = static_cast<std::int16_t>(r.read_u16_le());
        line.end = static_cast<std::int16_t>(r.read_u16_le());
        line.offset = r.read_u32_le();
        out.lines.push_back(line);
    }

    return SpriteError::None;
}

SpriteError decode_sprite(std::span<const std::byte> entry,
                          const Palette& palette, Sprite& out) {
    out = Sprite{};

    SpriteHeader h;
    if (SpriteError e = read_sprite_header(entry, h); e != SpriteError::None) {
        return e;
    }

    // Read the pixel region and obtain decompressed indices.
    const std::uint64_t pixel_start =
        static_cast<std::uint64_t>(kSpriteHeaderSize) +
        static_cast<std::uint64_t>(h.height) * kSpriteLineSize;
    if (pixel_start + h.data_size > entry.size()) {
        return SpriteError::PixelDataTruncated;
    }
    const std::span<const std::byte> pixel_block =
        entry.subspan(static_cast<std::size_t>(pixel_start), h.data_size);

    std::vector<std::uint8_t> pixels;
    if (h.decompressed_size == 0) {
        if (h.data_size == 0 && !h.lines.empty()) {
            // No pixel data but lines reference it.
            // Allow only if all lines are empty.
        }
        pixels.assign(reinterpret_cast<const std::uint8_t*>(pixel_block.data()),
                      reinterpret_cast<const std::uint8_t*>(pixel_block.data()) +
                          h.data_size);
    } else {
        if (!inflate_all(pixel_block, pixels)) {
            return SpriteError::InflateFailed;
        }
        if (pixels.size() < h.decompressed_size) {
            return SpriteError::InflateFailed;
        }
    }

    // Allocate the RGBA buffer, fully transparent.
    out.width = h.width;
    out.height = h.height;
    out.palette_id = h.palette_id;
    out.rgba.assign(static_cast<std::size_t>(h.width) * h.height * 4, 0);

    for (std::uint16_t y = 0; y < h.height; ++y) {
        const SpriteLine& line = h.lines[y];
        if (line.begin >= line.end) {
            continue;  // empty line
        }
        // Columns outside [0, width) are invalid; clamp/reject.
        if (line.begin < 0 || line.end > h.width) {
            return SpriteError::LineOutOfBounds;
        }
        const std::uint64_t span = static_cast<std::uint64_t>(line.end - line.begin);
        const std::uint64_t need = static_cast<std::uint64_t>(line.offset) + span;
        if (need > pixels.size()) {
            return SpriteError::LineOutOfBounds;
        }
        for (std::int32_t x = line.begin; x < line.end; ++x) {
            const std::uint8_t idx =
                pixels[line.offset + static_cast<std::size_t>(x - line.begin)];
            if (idx == 0) {
                continue;  // transparent
            }
            const std::size_t p = static_cast<std::size_t>(idx) * 3;
            std::uint8_t* px =
                &out.rgba[(static_cast<std::size_t>(y) * h.width + x) * 4];
            px[0] = palette.rgb[p + 0];
            px[1] = palette.rgb[p + 1];
            px[2] = palette.rgb[p + 2];
            px[3] = 255;
        }
    }

    return SpriteError::None;
}

}  // namespace openmm6::image
