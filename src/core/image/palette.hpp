#ifndef STARHAVEN_CORE_IMAGE_PALETTE_HPP
#define STARHAVEN_CORE_IMAGE_PALETTE_HPP

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace starhaven::image {

// A 256-entry RGB palette, as carried by .LOD image and palette entries.
struct Palette {
    // 256 entries × 3 bytes (R, G, B), each 0-255.
    std::array<std::uint8_t, 256 * 3> rgb{};
};

// Outcome of extracting a palette from raw bytes.
enum class PaletteError {
    None,
    // Input too small for a 48-byte image header.
    TooSmall,
    // The 768 palette bytes are missing or truncated.
    PaletteTruncated,
};

// Extract the trailing 768-byte palette from a palette-only image entry (a
// 48-byte header with zero image fields followed by 768 RGB bytes), or from the
// tail of any image entry whose palette follows its pixel data. The palette is
// read from offset `header + dataSize`.
[[nodiscard]] PaletteError extract_palette(std::span<const std::byte> entry,
                                            std::uint32_t data_offset,
                                            Palette& out);

// Build the palette-only `palXXX` entry name from a numeric palette id, e.g.
// paletteId 2 -> "pal002". Used to look up a sprite's shared palette.
[[nodiscard]] std::string palette_entry_name(std::uint16_t palette_id);

}  // namespace starhaven::image

#endif  // STARHAVEN_CORE_IMAGE_PALETTE_HPP
