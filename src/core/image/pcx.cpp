#include "core/image/pcx.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <cstdint>
#include <vector>

namespace starhaven::image {

using detail::inflate_all;

namespace {

// Unpack one row's worth of PCX RLE: a byte with the top two bits set is a
// run count over the byte after it; anything else stands for itself.
bool unpack_row(std::span<const std::uint8_t> data, std::size_t& at, std::size_t need,
                std::vector<std::uint8_t>& row) {
    row.clear();
    while (row.size() < need) {
        if (at >= data.size()) {
            return false;
        }
        const std::uint8_t c = data[at++];
        if (c >= 0xC0) {
            if (at >= data.size()) {
                return false;
            }
            row.insert(row.end(), c & 0x3F, data[at++]);
        } else {
            row.push_back(c);
        }
    }
    return true;
}

}  // namespace

BitmapError decode_pcx_entry(std::span<const std::byte> entry, Bitmap& out) {
    out = Bitmap{};
    if (entry.size() < kHeaderSize) {
        return BitmapError::TooSmall;
    }

    io::ByteReader r{entry};
    r.seek(0x14);
    const std::uint32_t data_size = r.read_u32_le();
    r.seek(0x28);
    const std::uint32_t decompressed_size = r.read_u32_le();
    if (static_cast<std::uint64_t>(kHeaderSize) + data_size > entry.size()) {
        return BitmapError::BadDataSize;
    }

    const std::span<const std::byte> payload = entry.subspan(kHeaderSize, data_size);
    std::vector<std::uint8_t> file;
    if (decompressed_size == 0) {
        file.assign(reinterpret_cast<const std::uint8_t*>(payload.data()),
                    reinterpret_cast<const std::uint8_t*>(payload.data()) + payload.size());
    } else if (!inflate_all(payload, file)) {
        return BitmapError::InflateFailed;
    }

    // The PCX header proper: magic, encoding, depth, extents, planes.
    constexpr std::size_t kPcxHeader = 128;
    if (file.size() < kPcxHeader || file[0] != 0x0A || file[2] != 1) {
        return BitmapError::TooSmall;
    }
    const auto u16 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(file[at]) | (static_cast<std::uint32_t>(file[at + 1]) << 8);
    };
    const std::uint32_t width = u16(8) - u16(4) + 1;
    const std::uint32_t height = u16(10) - u16(6) + 1;
    const std::uint8_t bits = file[3];
    const std::uint8_t planes = file[65];
    const std::uint32_t bytes_per_line = u16(66);
    if (width == 0 || height == 0 || bits != 8 || (planes != 1 && planes != 3)) {
        return BitmapError::BadDimensions;
    }

    // 8-bit single-plane rows index the palette stored at the tail behind a
    // 0x0C marker; 3-plane rows are the R, G and B bytes back to back.
    std::span<const std::uint8_t> palette;
    if (planes == 1) {
        if (file.size() < kPaletteSize + 1 || file[file.size() - kPaletteSize - 1] != 0x0C) {
            return BitmapError::PaletteTruncated;
        }
        palette = std::span<const std::uint8_t>(file).subspan(file.size() - kPaletteSize);
    }

    out.width = static_cast<std::uint16_t>(width);
    out.height = static_cast<std::uint16_t>(height);
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
    const std::span<const std::uint8_t> packed =
        std::span<const std::uint8_t>(file).subspan(kPcxHeader);
    std::size_t at = 0;
    std::vector<std::uint8_t> row;
    for (std::uint32_t y = 0; y < height; ++y) {
        if (!unpack_row(packed, at, static_cast<std::size_t>(bytes_per_line) * planes, row)) {
            return BitmapError::BadDataSize;
        }
        std::uint8_t* px = &out.rgba[static_cast<std::size_t>(y) * width * 4];
        for (std::uint32_t x = 0; x < width; ++x) {
            if (planes == 3) {
                px[x * 4 + 0] = row[x];
                px[x * 4 + 1] = row[bytes_per_line + x];
                px[x * 4 + 2] = row[2 * static_cast<std::size_t>(bytes_per_line) + x];
            } else {
                const std::size_t p = static_cast<std::size_t>(row[x]) * 3;
                px[x * 4 + 0] = palette[p + 0];
                px[x * 4 + 1] = palette[p + 1];
                px[x * 4 + 2] = palette[p + 2];
            }
        }
    }
    return BitmapError::None;
}

}  // namespace starhaven::image
