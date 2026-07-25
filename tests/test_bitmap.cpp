// Tests for the MM6 .LOD bitmap decoder.
//
// Fixtures are SYNTHETIC: hand-built from docs/formats/bitmap.md, with any
// compressed cases produced by zlib-compressing synthetic pixel bytes. No bytes
// from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "core/image/bitmap.hpp"

using namespace starhaven::image;

namespace {

void put_u16_le(std::vector<std::byte>& v, std::size_t off, std::uint16_t x) {
    v[off] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
}
void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

// zlib-compress src into dst. Returns false on zlib error.
bool zlib_compress(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()), static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

// Build an uncompressed 1x1 entry: palette index 5, palette entry 5 = (10,20,30).
std::vector<std::byte> make_uncompressed_1x1(std::uint32_t flags = 0) {
    std::vector<std::byte> v(kHeaderSize + /*pixels*/ 1 + kPaletteSize, std::byte{0});
    put_u32_le(v, 0x10, /*size*/ 1);
    put_u32_le(v, 0x14, /*dataSize*/ 1);
    put_u16_le(v, 0x18, /*width*/ 1);
    put_u16_le(v, 0x1A, /*height*/ 1);
    put_u32_le(v, 0x28, /*decompressedSize*/ 0);
    put_u32_le(v, 0x2C, /*flags*/ flags);

    // One pixel, palette index 5.
    v[kHeaderSize] = static_cast<std::byte>(5);

    // Palette: entry 5 = (R=10, G=20, B=30).
    const std::size_t p = kHeaderSize + 1 + 5 * 3;
    v[p + 0] = static_cast<std::byte>(10);
    v[p + 1] = static_cast<std::byte>(20);
    v[p + 2] = static_cast<std::byte>(30);
    return v;
}

}  // namespace

TEST_CASE("uncompressed 1x1 decodes to one RGBA pixel", "[bitmap]") {
    Bitmap b;
    auto entry = make_uncompressed_1x1();
    REQUIRE(decode_bitmap(entry, b) == BitmapError::None);
    REQUIRE(b.width == 1);
    REQUIRE(b.height == 1);
    REQUIRE(b.rgba.size() == 4);
    REQUIRE(b.rgba[0] == 10);   // R
    REQUIRE(b.rgba[1] == 20);   // G
    REQUIRE(b.rgba[2] == 30);   // B
    REQUIRE(b.rgba[3] == 255);  // opaque
}

TEST_CASE("uncompressed 2x2 maps each index through the palette", "[bitmap]") {
    // 2x2 image, indices 0,1,2,3 with distinct palette colors.
    std::vector<std::byte> v(kHeaderSize + 4 + kPaletteSize, std::byte{0});
    put_u32_le(v, 0x10, 4);
    put_u32_le(v, 0x14, 4);
    put_u16_le(v, 0x18, 2);
    put_u16_le(v, 0x1A, 2);
    put_u32_le(v, 0x28, 0);
    v[kHeaderSize + 0] = static_cast<std::byte>(0);
    v[kHeaderSize + 1] = static_cast<std::byte>(1);
    v[kHeaderSize + 2] = static_cast<std::byte>(2);
    v[kHeaderSize + 3] = static_cast<std::byte>(3);
    const std::size_t pal = kHeaderSize + 4;
    for (int i = 0; i < 4; ++i) {
        v[pal + i * 3 + 0] = static_cast<std::byte>(i * 10);
        v[pal + i * 3 + 1] = static_cast<std::byte>(i * 10 + 1);
        v[pal + i * 3 + 2] = static_cast<std::byte>(i * 10 + 2);
    }

    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::None);
    REQUIRE(b.width == 2);
    REQUIRE(b.height == 2);
    REQUIRE(b.rgba.size() == 16);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(b.rgba[i * 4 + 0] == i * 10);
        REQUIRE(b.rgba[i * 4 + 1] == i * 10 + 1);
        REQUIRE(b.rgba[i * 4 + 2] == i * 10 + 2);
        REQUIRE(b.rgba[i * 4 + 3] == 255);
    }
}

TEST_CASE("transparency flag makes index 0 fully transparent", "[bitmap]") {
    auto entry = make_uncompressed_1x1(/*flags*/ 0x0200);
    // Set the single pixel to index 0.
    entry[kHeaderSize] = std::byte{0};

    Bitmap b;
    REQUIRE(decode_bitmap(entry, b) == BitmapError::None);
    REQUIRE(b.rgba[3] == 0);  // alpha 0
}

TEST_CASE("zlib-compressed pixel data is inflated and decoded", "[bitmap]") {
    // 2x2 image whose pixel data is zlib-compressed.
    std::vector<std::uint8_t> pixels = {7, 7, 7, 7};
    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(pixels, compressed));

    std::vector<std::byte> v(kHeaderSize + compressed.size() + kPaletteSize, std::byte{0});
    put_u32_le(v, 0x10, 4);
    put_u32_le(v, 0x14, static_cast<std::uint32_t>(compressed.size()));
    put_u16_le(v, 0x18, 2);
    put_u16_le(v, 0x1A, 2);
    put_u32_le(v, 0x28, /*decompressedSize*/ 4);
    std::memcpy(&v[kHeaderSize], compressed.data(), compressed.size());
    const std::size_t pal = kHeaderSize + compressed.size();
    v[pal + 7 * 3 + 0] = static_cast<std::byte>(200);
    v[pal + 7 * 3 + 1] = static_cast<std::byte>(100);
    v[pal + 7 * 3 + 2] = static_cast<std::byte>(50);

    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::None);
    REQUIRE(b.width == 2);
    REQUIRE(b.height == 2);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(b.rgba[i * 4 + 0] == 200);
        REQUIRE(b.rgba[i * 4 + 1] == 100);
        REQUIRE(b.rgba[i * 4 + 2] == 50);
    }
}

TEST_CASE("truncated header is rejected", "[bitmap]") {
    std::vector<std::byte> v(kHeaderSize - 1, std::byte{0});
    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::TooSmall);
}

TEST_CASE("size != width*height is rejected", "[bitmap]") {
    auto v = make_uncompressed_1x1();
    put_u32_le(v, 0x10, /*size*/ 99);  // should be 1
    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::BadDimensions);
}

TEST_CASE("truncated palette is rejected", "[bitmap]") {
    auto v = make_uncompressed_1x1();
    // Drop the last byte of the palette.
    v.pop_back();
    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::PaletteTruncated);
}

TEST_CASE("corrupt zlib pixel data is rejected", "[bitmap]") {
    std::vector<std::byte> v(kHeaderSize + 16 + kPaletteSize, std::byte{0});
    put_u32_le(v, 0x10, 4);
    put_u32_le(v, 0x14, 16);
    put_u16_le(v, 0x18, 2);
    put_u16_le(v, 0x1A, 2);
    put_u32_le(v, 0x28, /*decompressedSize*/ 4);
    // Fill pixel region with garbage that is not valid zlib.
    for (std::size_t i = 0; i < 16; ++i) {
        v[kHeaderSize + i] = static_cast<std::byte>(0xFF);
    }
    Bitmap b;
    REQUIRE(decode_bitmap(v, b) == BitmapError::InflateFailed);
}
