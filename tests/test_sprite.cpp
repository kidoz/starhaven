// Tests for the MM6 .LOD sprite decoder.
//
// Fixtures are SYNTHETIC: hand-built from docs/formats/sprite.md, with
// compressed cases produced by zlib-compressing synthetic pixel bytes. No bytes
// from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "core/image/palette.hpp"
#include "core/image/sprite.hpp"

using namespace starhaven::image;

namespace {

void put_u16_le(std::vector<std::byte>& v, std::size_t off, std::uint16_t x) {
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
}
void put_i16_le(std::vector<std::byte>& v, std::size_t off, std::int16_t x) {
    put_u16_le(v, off, static_cast<std::uint16_t>(x));
}
void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

bool zlib_compress(const std::vector<std::uint8_t>& src,
                   std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()),
                  static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

// Build a 4-wide, 2-tall uncompressed sprite with one visible span per line.
// Line 0: cols [1,3) -> indices 5,5. Line 1: cols [0,2) -> indices 5,0.
std::vector<std::byte> make_simple_sprite(std::uint16_t palette_id = 2) {
    constexpr std::uint16_t kWidth = 4;
    constexpr std::uint16_t kHeight = 2;
    // pixel data: line0 has 2 bytes (5,5), line1 has 2 bytes (5,0)
    const std::vector<std::uint8_t> pixels = {5, 5, 5, 0};
    const std::uint32_t data_size = static_cast<std::uint32_t>(pixels.size());

    std::vector<std::byte> v(32 + kHeight * 8 + data_size, std::byte{0});
    // name[12] left zero.
    put_u32_le(v, 0x0C, data_size);
    put_u16_le(v, 0x10, kWidth);
    put_u16_le(v, 0x12, kHeight);
    put_u16_le(v, 0x14, palette_id);
    put_u32_le(v, 0x1C, /*decompressedSize*/ 0);

    // line 0: begin=1 end=3 offset=0
    put_i16_le(v, 32 + 0 * 8 + 0, 1);
    put_i16_le(v, 32 + 0 * 8 + 2, 3);
    put_u32_le(v, 32 + 0 * 8 + 4, 0);
    // line 1: begin=0 end=2 offset=2
    put_i16_le(v, 32 + 1 * 8 + 0, 0);
    put_i16_le(v, 32 + 1 * 8 + 2, 2);
    put_u32_le(v, 32 + 1 * 8 + 4, 2);

    std::memcpy(&v[32 + kHeight * 8], pixels.data(), pixels.size());
    return v;
}

Palette make_palette_with_entry(std::uint8_t idx, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b) {
    Palette p{};
    p.rgb[static_cast<std::size_t>(idx) * 3 + 0] = r;
    p.rgb[static_cast<std::size_t>(idx) * 3 + 1] = g;
    p.rgb[static_cast<std::size_t>(idx) * 3 + 2] = b;
    return p;
}

std::uint8_t alpha_at(const Sprite& s, int x, int y) {
    return s.rgba[(static_cast<std::size_t>(y) * s.width + x) * 4 + 3];
}

}  // namespace

TEST_CASE("simple sprite decodes visible spans and leaves rest transparent", "[sprite]") {
    auto entry = make_simple_sprite();
    const Palette pal = make_palette_with_entry(5, 10, 20, 30);

    Sprite s;
    REQUIRE(decode_sprite(entry, pal, s) == SpriteError::None);
    REQUIRE(s.width == 4);
    REQUIRE(s.height == 2);
    REQUIRE(s.palette_id == 2);

    // Line 0: cols 1,2 visible (index 5 -> (10,20,30,255)); cols 0,3 transparent.
    REQUIRE(alpha_at(s, 0, 0) == 0);
    REQUIRE(alpha_at(s, 1, 0) == 255);
    REQUIRE(alpha_at(s, 2, 0) == 255);
    REQUIRE(alpha_at(s, 3, 0) == 0);
    REQUIRE(s.rgba[(0 * 4 + 1) * 4 + 0] == 10);

    // Line 1: col 0 visible (index 5); col 1 transparent (index 0).
    REQUIRE(alpha_at(s, 0, 1) == 255);
    REQUIRE(alpha_at(s, 1, 1) == 0);
}

TEST_CASE("index 0 inside a span is transparent", "[sprite]") {
    auto entry = make_simple_sprite();
    const Palette pal = make_palette_with_entry(5, 1, 2, 3);

    Sprite s;
    REQUIRE(decode_sprite(entry, pal, s) == SpriteError::None);
    // Line 1, col 1: index 0 -> transparent even though inside [0,2).
    REQUIRE(alpha_at(s, 1, 1) == 0);
}

TEST_CASE("read_sprite_header reports dimensions and palette id", "[sprite]") {
    auto entry = make_simple_sprite(/*palette_id*/ 7);
    SpriteHeader h;
    REQUIRE(read_sprite_header(entry, h) == SpriteError::None);
    REQUIRE(h.width == 4);
    REQUIRE(h.height == 2);
    REQUIRE(h.palette_id == 7);
    REQUIRE(h.data_size == 4);
    REQUIRE(h.lines.size() == 2);
    REQUIRE(h.lines[0].begin == 1);
    REQUIRE(h.lines[0].end == 3);
    REQUIRE(h.lines[1].offset == 2);
}

TEST_CASE("palette_entry_name formats zero-padded ids", "[palette]") {
    REQUIRE(palette_entry_name(2) == "pal002");
    REQUIRE(palette_entry_name(42) == "pal042");
    REQUIRE(palette_entry_name(0) == "pal000");
}

TEST_CASE("zlib-compressed sprite pixel data is inflated", "[sprite]") {
    // Same layout as make_simple_sprite but with compressed pixels.
    constexpr std::uint16_t kWidth = 4;
    constexpr std::uint16_t kHeight = 2;
    const std::vector<std::uint8_t> raw_pixels = {5, 5, 5, 0};
    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(raw_pixels, compressed));

    std::vector<std::byte> v(32 + kHeight * 8 + compressed.size(), std::byte{0});
    put_u32_le(v, 0x0C, static_cast<std::uint32_t>(compressed.size()));
    put_u16_le(v, 0x10, kWidth);
    put_u16_le(v, 0x12, kHeight);
    put_u16_le(v, 0x14, 2);
    put_u32_le(v, 0x1C, /*decompressedSize*/ 4);
    put_i16_le(v, 32 + 0, 1); put_i16_le(v, 34, 3); put_u32_le(v, 36, 0);
    put_i16_le(v, 40, 0); put_i16_le(v, 42, 2); put_u32_le(v, 44, 2);
    std::memcpy(&v[32 + kHeight * 8], compressed.data(), compressed.size());

    Sprite s;
    REQUIRE(decode_sprite(v, make_palette_with_entry(5, 9, 8, 7), s) == SpriteError::None);
    REQUIRE(alpha_at(s, 1, 0) == 255);
}

TEST_CASE("truncated header is rejected", "[sprite]") {
    std::vector<std::byte> v(31, std::byte{0});
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::TooSmall);
}

TEST_CASE("zero width or height is rejected", "[sprite]") {
    auto v = make_simple_sprite();
    put_u16_le(v, 0x10, 0);  // width 0
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::BadDimensions);
}

TEST_CASE("line table extending past buffer is rejected", "[sprite]") {
    auto v = make_simple_sprite();
    // Lie about height so the line table overflows.
    put_u16_le(v, 0x12, 999);
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::LineTableTruncated);
}

TEST_CASE("dataSize inconsistent with total size is rejected", "[sprite]") {
    auto v = make_simple_sprite();
    put_u32_le(v, 0x0C, 12345);  // wrong dataSize
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::BadDataSize);
}

TEST_CASE("line offset past end of pixel data is rejected", "[sprite]") {
    auto v = make_simple_sprite();
    // Point line 0 at an offset beyond the 4 pixel bytes.
    put_u32_le(v, 36, 100);
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::LineOutOfBounds);
}

TEST_CASE("corrupt zlib pixel data is rejected", "[sprite]") {
    constexpr std::uint16_t kWidth = 4;
    constexpr std::uint16_t kHeight = 1;
    std::vector<std::byte> v(32 + kHeight * 8 + 16, std::byte{0});
    put_u32_le(v, 0x0C, 16);
    put_u16_le(v, 0x10, kWidth);
    put_u16_le(v, 0x12, kHeight);
    put_u32_le(v, 0x1C, 4);
    put_i16_le(v, 32, 0); put_i16_le(v, 34, 4); put_u32_le(v, 36, 0);
    for (std::size_t i = 0; i < 16; ++i) {
        v[32 + kHeight * 8 + i] = static_cast<std::byte>(0xFF);
    }
    Sprite s;
    REQUIRE(decode_sprite(v, Palette{}, s) == SpriteError::InflateFailed);
}
