#include "core/image/font.hpp"

#include <cstddef>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::image {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;
constexpr std::size_t kFirstCharOffset = 0x00;
constexpr std::size_t kLastCharOffset = 0x01;
constexpr std::size_t kHeightOffset = 0x05;

}  // namespace

FontError Font::parse(std::span<const std::byte> entry, Font& out) {
    out = Font{};

    if (entry.size() <= kEntryHeaderSize) {
        return FontError::TooSmall;
    }
    std::vector<std::uint8_t> raw;
    if (!detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return FontError::NotCompressed;
    }

    const std::size_t metrics = kFontHeaderSize;
    const std::size_t offsets = metrics + kFontCharCount * kFontMetricSize;
    const std::size_t pixels = offsets + kFontCharCount * 4;
    if (raw.size() <= pixels) {
        return FontError::BadHeader;
    }

    out.first_char_ = raw[kFirstCharOffset];
    out.last_char_ = raw[kLastCharOffset];
    out.height_ = raw[kHeightOffset];
    if (out.height_ <= 0) {
        return FontError::BadHeader;
    }

    const auto bytes = std::as_bytes(std::span<const std::uint8_t>(raw));
    io::ByteReader r(bytes);

    out.glyphs_.assign(kFontCharCount, FontGlyph{});
    std::vector<std::uint32_t> starts(kFontCharCount, 0);
    for (std::size_t c = 0; c < kFontCharCount; ++c) {
        if (!r.seek(metrics + c * kFontMetricSize)) {
            return FontError::BadHeader;
        }
        out.glyphs_[c].left_spacing = r.read_i32_le();
        out.glyphs_[c].width = r.read_i32_le();
        out.glyphs_[c].right_spacing = r.read_i32_le();
        if (!r.seek(offsets + c * 4)) {
            return FontError::BadHeader;
        }
        starts[c] = r.read_u32_le();
        if (!r.ok()) {
            return FontError::BadHeader;
        }
    }

    // Each defined glyph occupies exactly width × height bytes, and the
    // glyphs tile the pixel region in character order with nothing between
    // them and nothing left over. Thirteen of the game's fourteen fonts
    // satisfy that exactly; the one that does not is rejected here rather
    // than decoded into nonsense. See docs/formats/font.md.
    out.pixels_.assign(raw.begin() + static_cast<std::ptrdiff_t>(pixels), raw.end());
    const std::size_t region = out.pixels_.size();

    // A character the font does not define has zero metrics. Its offset is
    // zero too, but so is the first real glyph's in a hand-made font, so the
    // width is what decides.
    std::size_t previous = kFontCharCount;
    for (std::size_t c = 0; c < kFontCharCount; ++c) {
        if (out.glyphs_[c].width <= 0) {
            out.glyphs_[c] = FontGlyph{};
            continue;
        }
        const auto size =
            static_cast<std::size_t>(out.glyphs_[c].width) * static_cast<std::size_t>(out.height_);
        if (starts[c] + size > region) {
            return FontError::BadGlyphTable;
        }
        if (previous != kFontCharCount) {
            const auto previous_size = static_cast<std::size_t>(out.glyphs_[previous].width) *
                                       static_cast<std::size_t>(out.height_);
            if (starts[c] != starts[previous] + previous_size) {
                return FontError::BadGlyphTable;
            }
        }
        out.glyphs_[c].pixels = std::span<const std::uint8_t>(out.pixels_).subspan(starts[c], size);
        ++out.glyph_count_;
        previous = c;
    }
    if (out.glyph_count_ == 0) {
        return FontError::BadGlyphTable;
    }
    return FontError::None;
}

const FontGlyph* Font::glyph(std::uint8_t code) const noexcept {
    if (glyphs_.empty() || glyphs_[code].pixels.empty()) {
        return nullptr;
    }
    return &glyphs_[code];
}

std::int32_t Font::text_width(std::string_view text) const noexcept {
    std::int32_t total = 0;
    for (const char c : text) {
        if (const FontGlyph* g = glyph(static_cast<std::uint8_t>(c)); g != nullptr) {
            total += g->advance();
        }
    }
    return total;
}

}  // namespace starhaven::image
