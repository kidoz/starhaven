#include "core/image/palette.hpp"

#include <cstring>
#include <format>

namespace openmm6::image {

namespace {

constexpr std::uint32_t kPaletteSize = 768;  // 256 * 3

}  // namespace

PaletteError extract_palette(std::span<const std::byte> entry,
                             std::uint32_t data_offset, Palette& out) {
    out = Palette{};

    const std::uint64_t palette_start = data_offset;
    const std::uint64_t palette_end =
        static_cast<std::uint64_t>(palette_start) + kPaletteSize;
    if (entry.size() < 48) {
        return PaletteError::TooSmall;
    }
    if (palette_end > entry.size()) {
        return PaletteError::PaletteTruncated;
    }

    std::memcpy(out.rgb.data(), entry.data() + palette_start, kPaletteSize);
    return PaletteError::None;
}

std::string palette_entry_name(std::uint16_t palette_id) {
    return std::format("pal{:03}", palette_id);
}

}  // namespace openmm6::image
