#ifndef STARHAVEN_CORE_IMAGE_FONT_HPP
#define STARHAVEN_CORE_IMAGE_FONT_HPP

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace starhaven::image {

// One character of a bitmap font. `pixels` is `width × height` bytes, one per
// pixel, row-major from the top.
//
// Only three values ever appear: 0 leaves the background alone, 1 is the
// glyph's body, and 255 is its outline. The font says nothing about what
// colours those are, so a caller supplies them.
struct FontGlyph {
    std::int32_t left_spacing = 0;
    std::int32_t width = 0;
    std::int32_t right_spacing = 0;
    std::span<const std::uint8_t> pixels;

    // How far the pen advances after drawing this glyph.
    [[nodiscard]] std::int32_t advance() const noexcept {
        return left_spacing + width + right_spacing;
    }
};

inline constexpr std::uint8_t kFontPixelTransparent = 0;
inline constexpr std::uint8_t kFontPixelBody = 1;
inline constexpr std::uint8_t kFontPixelOutline = 255;

enum class FontError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadHeader,      // the block cannot hold the metric and offset tables
    BadGlyphTable,  // the glyph offsets are not consistent with the metrics
};

// A bitmap font from `icons.lod`. See docs/formats/font.md.
class Font {
public:
    Font() = default;

    // `entry` is the raw stored bytes of an archive `.FNT` entry.
    [[nodiscard]] static FontError parse(std::span<const std::byte> entry, Font& out);

    [[nodiscard]] std::int32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint8_t first_char() const noexcept { return first_char_; }
    [[nodiscard]] std::uint8_t last_char() const noexcept { return last_char_; }
    [[nodiscard]] std::size_t glyph_count() const noexcept { return glyph_count_; }

    // The glyph for a byte, or nullptr when the font does not define it.
    // Text is Windows-1252, the encoding the game's own tables use.
    [[nodiscard]] const FontGlyph* glyph(std::uint8_t code) const noexcept;

    // Total advance of a string, for centring and right-aligning. Bytes the
    // font does not define contribute nothing.
    [[nodiscard]] std::int32_t text_width(std::string_view text) const noexcept;

private:
    std::vector<FontGlyph> glyphs_;  // 256 entries, empty where undefined
    std::vector<std::uint8_t> pixels_;
    std::int32_t height_ = 0;
    std::uint8_t first_char_ = 0;
    std::uint8_t last_char_ = 0;
    std::size_t glyph_count_ = 0;
};

// Layout constants (see docs/formats/font.md).
inline constexpr std::size_t kFontHeaderSize = 32;
inline constexpr std::size_t kFontCharCount = 256;
inline constexpr std::size_t kFontMetricSize = 12;

}  // namespace starhaven::image

#endif  // STARHAVEN_CORE_IMAGE_FONT_HPP
