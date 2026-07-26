// Tests for the bitmap font decoder.
//
// Hermetic: every fixture is synthesized from the format described in
// docs/formats/font.md. No bytes are copied from a game archive.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include "core/image/font.hpp"

using namespace starhaven::image;

namespace {

struct GlyphSpec {
    std::int32_t left = 0;
    std::int32_t width = 0;
    std::int32_t right = 0;
};

void put_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF);
    }
}

// Build a font whose glyphs tile the pixel region in character order, as the
// shipped fonts do.
std::vector<std::uint8_t> make_body(const std::map<std::uint8_t, GlyphSpec>& glyphs,
                                    std::uint8_t height, std::uint8_t first, std::uint8_t last) {
    const std::size_t metrics = kFontHeaderSize;
    const std::size_t offsets = metrics + kFontCharCount * kFontMetricSize;
    const std::size_t pixels = offsets + kFontCharCount * 4;

    std::vector<std::uint8_t> body(pixels, 0);
    body[0x00] = first;
    body[0x01] = last;
    body[0x05] = height;

    std::uint32_t cursor = 0;
    for (const auto& [code, g] : glyphs) {
        const std::size_t m = metrics + static_cast<std::size_t>(code) * kFontMetricSize;
        put_u32(body, m, static_cast<std::uint32_t>(g.left));
        put_u32(body, m + 4, static_cast<std::uint32_t>(g.width));
        put_u32(body, m + 8, static_cast<std::uint32_t>(g.right));
        put_u32(body, offsets + static_cast<std::size_t>(code) * 4, cursor);
        cursor += static_cast<std::uint32_t>(g.width) * height;
    }
    // Fill the pixel region so a glyph's bytes are recognisable.
    body.resize(pixels + cursor, kFontPixelBody);
    return body;
}

std::vector<std::byte> wrap(const std::vector<std::uint8_t>& body) {
    uLongf cap = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::uint8_t> z(cap);
    REQUIRE(compress(z.data(), &cap, body.data(), static_cast<uLong>(body.size())) == Z_OK);
    z.resize(cap);

    std::vector<std::byte> out(48, std::byte{0});
    const char* name = "test.fnt";
    std::memcpy(out.data(), name, std::strlen(name));
    for (const std::uint8_t b : z)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

// 'A' is 6 wide, 'B' 4, 'C' 8 — enough to tell advance from width.
const std::map<std::uint8_t, GlyphSpec> kSample{
    {'A', {1, 6, 1}},
    {'B', {0, 4, 2}},
    {'C', {2, 8, 0}},
};

}  // namespace

TEST_CASE("a font decodes its header and glyph metrics", "[font]") {
    Font f;
    REQUIRE(Font::parse(wrap(make_body(kSample, 17, 'A', 'C')), f) == FontError::None);
    REQUIRE(f.height() == 17);
    REQUIRE(f.first_char() == 'A');
    REQUIRE(f.last_char() == 'C');
    REQUIRE(f.glyph_count() == 3);

    REQUIRE(f.glyph('A')->width == 6);
    REQUIRE(f.glyph('A')->left_spacing == 1);
    REQUIRE(f.glyph('A')->right_spacing == 1);
    REQUIRE(f.glyph('C')->width == 8);
}

TEST_CASE("the first glyph may sit at offset zero", "[font]") {
    // Every shipped font's first character starts at pixel offset 0, so
    // treating a zero offset as "undefined" silently drops one glyph per font.
    // The width is what says whether a character exists.
    Font f;
    REQUIRE(Font::parse(wrap(make_body(kSample, 17, 'A', 'C')), f) == FontError::None);
    REQUIRE(f.first_char() == 'A');
    REQUIRE(f.glyph('A') != nullptr);
    REQUIRE(f.glyph('A')->pixels.size() == 6 * 17);
}

TEST_CASE("a glyph owns width by height pixels", "[font]") {
    Font f;
    REQUIRE(Font::parse(wrap(make_body(kSample, 17, 'A', 'C')), f) == FontError::None);
    REQUIRE(f.glyph('A')->pixels.size() == 6 * 17);
    REQUIRE(f.glyph('C')->pixels.size() == 8 * 17);
}

TEST_CASE("an undefined character has no glyph", "[font]") {
    Font f;
    REQUIRE(Font::parse(wrap(make_body(kSample, 17, 'A', 'C')), f) == FontError::None);
    REQUIRE(f.glyph('Z') == nullptr);
    REQUIRE(f.glyph(0) == nullptr);
}

TEST_CASE("text width sums the advances, not the widths", "[font]") {
    Font f;
    REQUIRE(Font::parse(wrap(make_body(kSample, 17, 'A', 'C')), f) == FontError::None);
    // A advances 1+6+1, B 0+4+2, C 2+8+0.
    REQUIRE(f.text_width("A") == 8);
    REQUIRE(f.text_width("ABC") == 8 + 6 + 10);
    // Characters the font does not define contribute nothing.
    REQUIRE(f.text_width("AZC") == 8 + 10);
    REQUIRE(f.text_width("") == 0);
}

TEST_CASE("glyphs that do not tile the pixel region are rejected", "[font]") {
    // One shipped font, calig.fnt, fails exactly this check. Decoding it
    // anyway would produce glyphs sliced out of the wrong bytes.
    auto body = make_body(kSample, 17, 'A', 'C');
    const std::size_t offsets = kFontHeaderSize + kFontCharCount * kFontMetricSize;
    put_u32(body, offsets + static_cast<std::size_t>('B') * 4, 999);

    Font f;
    REQUIRE(Font::parse(wrap(body), f) == FontError::BadGlyphTable);
}

TEST_CASE("a glyph running past the pixel region is rejected", "[font]") {
    auto body = make_body(kSample, 17, 'A', 'C');
    const std::size_t metrics = kFontHeaderSize;
    put_u32(body, metrics + static_cast<std::size_t>('C') * kFontMetricSize + 4, 9999);

    Font f;
    REQUIRE(Font::parse(wrap(body), f) == FontError::BadGlyphTable);
}

TEST_CASE("malformed entries are rejected", "[font]") {
    Font f;
    const std::vector<std::byte> tiny(20, std::byte{0});
    REQUIRE(Font::parse(tiny, f) == FontError::TooSmall);

    const std::vector<std::byte> junk(80, std::byte{0});
    REQUIRE(Font::parse(junk, f) == FontError::NotCompressed);

    // A block too small for the metric and offset tables.
    REQUIRE(Font::parse(wrap(std::vector<std::uint8_t>(64, 0)), f) == FontError::BadHeader);

    // A height of zero would make every glyph empty.
    auto body = make_body(kSample, 0, 'A', 'C');
    REQUIRE(Font::parse(wrap(body), f) == FontError::BadHeader);
}
