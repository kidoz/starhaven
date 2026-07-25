// Tests for the texture container and its sampler.
//
// Hermetic: every texture here is synthetic. Texture bytes in the real engine
// come from parsed .LOD entries, so the validation cases below stand in for
// malformed or truncated archive data.
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/render/texture.hpp"

using namespace starhaven::render;

namespace {

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kGreen{0, 255, 0, 255};
constexpr Color kBlue{0, 0, 255, 255};
constexpr Color kWhite{255, 255, 255, 255};

bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A 2x2 texture laid out top-to-bottom:
//   red   green
//   blue  white
Texture make_quad_texture() {
    const std::vector<std::uint8_t> rgba{
        255, 0,   0,   255,  // (0,0) red
        0,   255, 0,   255,  // (1,0) green
        0,   0,   255, 255,  // (0,1) blue
        255, 255, 255, 255   // (1,1) white
    };
    Texture t;
    REQUIRE(Texture::create(2, 2, rgba, t));
    return t;
}

}  // namespace

TEST_CASE("create rejects non-positive dimensions", "[texture]") {
    Texture t;
    REQUIRE_FALSE(Texture::create(0, 4, std::vector<std::uint8_t>(0), t));
    REQUIRE_FALSE(Texture::create(4, 0, std::vector<std::uint8_t>(0), t));
    REQUIRE_FALSE(Texture::create(-2, 4, std::vector<std::uint8_t>(32), t));
    REQUIRE(t.empty());
}

TEST_CASE("create rejects a buffer whose size does not match the dimensions", "[texture]") {
    Texture t;
    // 2x2 RGBA needs exactly 16 bytes. A truncated archive entry must not
    // produce a sampler that reads past the end.
    REQUIRE_FALSE(Texture::create(2, 2, std::vector<std::uint8_t>(15), t));
    REQUIRE_FALSE(Texture::create(2, 2, std::vector<std::uint8_t>(17), t));
    REQUIRE(t.empty());
}

TEST_CASE("create accepts an exactly-sized buffer", "[texture]") {
    Texture t;
    REQUIRE(Texture::create(4, 3, std::vector<std::uint8_t>(4 * 3 * 4), t));
    REQUIRE(t.width() == 4);
    REQUIRE(t.height() == 3);
    REQUIRE_FALSE(t.empty());
    REQUIRE(t.pixels().size() == 48);
}

TEST_CASE("a failed create leaves the output empty rather than stale", "[texture]") {
    Texture t = make_quad_texture();
    REQUIRE_FALSE(t.empty());
    // Reusing the same object for a bad load must not leave the old pixels
    // reachable behind new dimensions.
    REQUIRE_FALSE(Texture::create(8, 8, std::vector<std::uint8_t>(4), t));
    REQUIRE(t.empty());
    REQUIRE(t.width() == 0);
    REQUIRE(t.height() == 0);
}

TEST_CASE("sample maps v=0 to the top row", "[texture]") {
    const Texture t = make_quad_texture();
    // Row order is top-to-bottom, matching the .LOD image decoders.
    REQUIRE(same(t.sample(0.25f, 0.25f, WrapMode::Clamp), kRed));
    REQUIRE(same(t.sample(0.75f, 0.25f, WrapMode::Clamp), kGreen));
    REQUIRE(same(t.sample(0.25f, 0.75f, WrapMode::Clamp), kBlue));
    REQUIRE(same(t.sample(0.75f, 0.75f, WrapMode::Clamp), kWhite));
}

TEST_CASE("sample with Repeat tiles the texture", "[texture]") {
    const Texture t = make_quad_texture();
    // u past the right edge wraps to the same texel one period back.
    REQUIRE(
        same(t.sample(1.25f, 0.25f, WrapMode::Repeat), t.sample(0.25f, 0.25f, WrapMode::Repeat)));
    REQUIRE(same(t.sample(4.75f, 0.25f, WrapMode::Repeat), kGreen));
    // Negative u must wrap forward, not clamp: -0.25 lands in the right half.
    REQUIRE(same(t.sample(-0.25f, 0.25f, WrapMode::Repeat), kGreen));
    REQUIRE(same(t.sample(0.25f, -0.25f, WrapMode::Repeat), kBlue));
}

TEST_CASE("sample with Clamp holds the edge texel", "[texture]") {
    const Texture t = make_quad_texture();
    REQUIRE(same(t.sample(-5.0f, 0.25f, WrapMode::Clamp), kRed));
    REQUIRE(same(t.sample(5.0f, 0.25f, WrapMode::Clamp), kGreen));
    REQUIRE(same(t.sample(0.25f, 5.0f, WrapMode::Clamp), kBlue));
    // Exactly 1.0 must resolve to the last texel, not one past it.
    REQUIRE(same(t.sample(1.0f, 1.0f, WrapMode::Clamp), kWhite));
}

TEST_CASE("texel resolves out-of-range integer coordinates", "[texture]") {
    const Texture t = make_quad_texture();
    REQUIRE(same(t.texel(-1, 0, WrapMode::Repeat), kGreen));
    REQUIRE(same(t.texel(2, 0, WrapMode::Repeat), kRed));
    REQUIRE(same(t.texel(0, -1, WrapMode::Repeat), kBlue));
    REQUIRE(same(t.texel(-1, 0, WrapMode::Clamp), kRed));
    REQUIRE(same(t.texel(9, 9, WrapMode::Clamp), kWhite));
}

TEST_CASE("sample tolerates non-finite coordinates", "[texture]") {
    const Texture t = make_quad_texture();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // A degenerate projection can produce these. They must resolve to a valid
    // texel rather than an undefined float->int conversion.
    REQUIRE(same(t.sample(nan, 0.5f, WrapMode::Repeat), kRed));
    REQUIRE(same(t.sample(0.5f, nan, WrapMode::Clamp), kRed));
    REQUIRE(same(t.sample(inf, 0.5f, WrapMode::Repeat), kRed));
    REQUIRE(same(t.sample(-inf, -inf, WrapMode::Clamp), kRed));
}

TEST_CASE("sample of a very large coordinate stays in range", "[texture]") {
    const Texture t = make_quad_texture();
    // Scaling before reducing would overflow int here; the sampler reduces
    // first, so this is well-defined and in-bounds.
    const Color c = t.sample(1.0e9f, 1.0e9f, WrapMode::Repeat);
    REQUIRE(c.a == 255);
}

TEST_CASE("an empty texture samples as transparent black", "[texture]") {
    const Texture t;
    REQUIRE(t.empty());
    const Color c = t.sample(0.5f, 0.5f, WrapMode::Repeat);
    REQUIRE(c.a == 0);
    REQUIRE(same(t.texel(0, 0, WrapMode::Clamp), Color{0, 0, 0, 0}));
}
