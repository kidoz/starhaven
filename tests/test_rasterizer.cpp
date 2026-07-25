// Tests for the software rasterizer.
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/render/rasterizer.hpp"
#include "core/render/texture.hpp"

using namespace starhaven::render;

namespace {

// Helper: count filled (non-background) pixels in a framebuffer.
int count_filled(const Framebuffer& fb, std::uint8_t bg) {
    int n = 0;
    auto c = fb.color();
    for (int i = 0; i < fb.width() * fb.height(); ++i) {
        if (c[i * 4] != bg)
            ++n;
    }
    return n;
}

bool is_filled(const Framebuffer& fb, int x, int y) {
    return fb.color()[(y * fb.width() + x) * 4] != 0;
}

Color pixel_at(const Framebuffer& fb, int x, int y) {
    auto c = fb.color();
    const int i = (y * fb.width() + x) * 4;
    return Color{c[i], c[i + 1], c[i + 2], c[i + 3]};
}

bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A 4x1 texture: red | green | blue | white, so a sampled u maps to an
// unambiguous, nameable color.
Texture make_strip_texture() {
    const std::vector<std::uint8_t> rgba{
        255, 0,   0,   255,  // u in [0.00,0.25)
        0,   255, 0,   255,  // u in [0.25,0.50)
        0,   0,   255, 255,  // u in [0.50,0.75)
        255, 255, 255, 255   // u in [0.75,1.00)
    };
    Texture t;
    REQUIRE(Texture::create(4, 1, rgba, t));
    return t;
}

Texture make_solid_texture(Color c) {
    const std::vector<std::uint8_t> rgba{c.r, c.g, c.b, c.a};
    Texture t;
    REQUIRE(Texture::create(1, 1, rgba, t));
    return t;
}

}  // namespace

TEST_CASE("clear fills the whole framebuffer", "[rasterizer]") {
    Framebuffer fb(8, 8);
    fb.clear({200, 200, 200, 255});
    REQUIRE(count_filled(fb, 0) == 64);
}

TEST_CASE("a small filled triangle covers its bounding box region", "[rasterizer]") {
    Framebuffer fb(8, 8);
    fb.clear({0, 0, 0, 255});
    // Triangle near the top-left corner.
    ScreenVertex a{1.5f, 1.5f, 0.0f, 1.0f};
    ScreenVertex b{5.5f, 1.5f, 0.0f, 1.0f};
    ScreenVertex c{1.5f, 5.5f, 0.0f, 1.0f};
    fb.draw_triangle(a, b, c, /*cull*/ false);
    REQUIRE(count_filled(fb, 0) > 0);
    // Center of the triangle must be filled.
    REQUIRE(is_filled(fb, 2, 2));
    // A far corner must remain empty.
    REQUIRE_FALSE(is_filled(fb, 7, 7));
}

TEST_CASE("z-buffer rejects a farther triangle drawn after a nearer one", "[rasterizer]") {
    Framebuffer fb(4, 4);
    fb.clear({0, 0, 0, 255});
    // Near triangle covering the whole area at z=0.2.
    ScreenVertex n0{0, 0, 0.2f, 1.0f};
    ScreenVertex n1{4, 0, 0.2f, 1.0f};
    ScreenVertex n2{0, 4, 0.2f, 1.0f};
    fb.draw_triangle(n0, n1, n2, false);
    const int near_count = count_filled(fb, 0);

    // Far triangle (same area, z=0.9) drawn after — must not overwrite.
    ScreenVertex f0{0, 0, 0.9f, 0.5f};
    ScreenVertex f1{4, 0, 0.9f, 0.5f};
    ScreenVertex f2{0, 4, 0.9f, 0.5f};
    fb.draw_triangle(f0, f1, f2, false);
    REQUIRE(count_filled(fb, 0) == near_count);
}

TEST_CASE("nearer triangle drawn later overwrites farther one", "[rasterizer]") {
    Framebuffer fb(4, 4);
    fb.clear({0, 0, 0, 255});
    ScreenVertex f0{0, 0, 0.9f, 0.5f};
    ScreenVertex f1{4, 0, 0.9f, 0.5f};
    ScreenVertex f2{0, 4, 0.9f, 0.5f};
    fb.draw_triangle(f0, f1, f2, false);
    ScreenVertex n0{0, 0, 0.2f, 1.0f};
    ScreenVertex n1{4, 0, 0.2f, 1.0f};
    ScreenVertex n2{0, 4, 0.2f, 1.0f};
    fb.draw_triangle(n0, n1, n2, false);
    // The nearer triangle (brightness 1) must win.
    REQUIRE(is_filled(fb, 1, 1));
}

TEST_CASE("near-plane clipping drops a fully-behind triangle", "[rasterizer]") {
    ViewVertex tri[3] = {{0, 0, -0.5f, 1}, {1, 0, -0.5f, 1}, {0, 1, -0.5f, 1}};
    std::vector<ViewVertex> out;
    clip_near(tri, /*near_z*/ -1.0f, out);  // all vertices behind near plane
    REQUIRE(out.empty());
}

TEST_CASE("near-plane clipping keeps a fully-in-front triangle intact", "[rasterizer]") {
    ViewVertex tri[3] = {{0, 0, -5, 1}, {1, 0, -5, 1}, {0, 1, -5, 1}};
    std::vector<ViewVertex> out;
    clip_near(tri, /*near_z*/ -1.0f, out);
    REQUIRE(out.size() == 3);  // one triangle, 3 vertices
}

TEST_CASE("near-plane clipping splits a straddling triangle into a quad (6 verts)",
          "[rasterizer]") {
    // Two vertices in front (z=-5), one behind (z=-0.5).
    ViewVertex tri[3] = {{0, 0, -5, 1}, {1, 0, -5, 1}, {0, 1, -0.5f, 1}};
    std::vector<ViewVertex> out;
    clip_near(tri, /*near_z*/ -1.0f, out);
    REQUIRE(out.size() == 6);  // a quad -> two triangles
}

// --- Textured rasterization -------------------------------------------------

TEST_CASE("near-plane clipping interpolates texture coordinates", "[rasterizer]") {
    // Two vertices in front with u=0, one behind with u=1. The vertices the
    // clipper introduces must carry the interpolated u, or the texture would
    // shear wherever geometry crosses the near plane.
    ViewVertex tri[3];
    tri[0] = ViewVertex{0, 0, -5, 1, 1, 1, 0.0f, 0.0f};
    tri[1] = ViewVertex{1, 0, -5, 1, 1, 1, 0.0f, 0.0f};
    tri[2] = ViewVertex{0, 1, -0.5f, 1, 1, 1, 1.0f, 0.0f};

    std::vector<ViewVertex> out;
    clip_near(tri, /*near_z*/ -1.0f, out);
    REQUIRE(out.size() == 6);

    // The crossing sits at t = (-1 - -5) / (-0.5 - -5) = 4 / 4.5, so the new
    // vertices carry u = 8/9. Every emitted u must be one of the originals or
    // that interpolated value -- never an unwritten 0 where 8/9 belongs.
    bool saw_interpolated = false;
    for (const ViewVertex& v : out) {
        const bool is_original = (v.u == 0.0f);
        const bool is_crossing = v.u == Catch::Approx(8.0 / 9.0).epsilon(1e-4);
        REQUIRE((is_original || is_crossing));
        if (is_crossing)
            saw_interpolated = true;
    }
    REQUIRE(saw_interpolated);
}

TEST_CASE("drawing with an empty texture leaves the framebuffer untouched", "[rasterizer]") {
    Framebuffer fb(8, 8);
    fb.clear({0, 0, 0, 255});
    const Texture empty;
    ScreenVertex a{0, 0, 0.5f};
    ScreenVertex b{8, 0, 0.5f};
    ScreenVertex c{0, 8, 0.5f};
    fb.draw_triangle_textured(a, b, c, empty, WrapMode::Repeat, false);
    REQUIRE(count_filled(fb, 0) == 0);
}

TEST_CASE("a textured triangle samples the texture through the vertex UVs", "[rasterizer]") {
    Framebuffer fb(8, 4);
    fb.clear({0, 0, 0, 255});
    const Texture strip = make_strip_texture();

    // A screen-aligned right triangle covering the left half, u spanning 0..1
    // across x. inv_w is left at its default of 1, so interpolation is affine
    // and the expected texel follows directly from the pixel's x position.
    ScreenVertex a{0, 0, 0.5f};
    a.u = 0.0f;
    a.v = 0.5f;
    ScreenVertex b{8, 0, 0.5f};
    b.u = 1.0f;
    b.v = 0.5f;
    ScreenVertex c{0, 4, 0.5f};
    c.u = 0.0f;
    c.v = 0.5f;
    fb.draw_triangle_textured(a, b, c, strip, WrapMode::Clamp, false);

    // Pixel (1,0): centre x=1.5 of 8 -> u=0.1875 -> first quarter -> red.
    REQUIRE(same(pixel_at(fb, 1, 0), Color{255, 0, 0, 255}));
    // Pixel (6,0): centre x=6.5 -> u=0.8125 -> last quarter -> white.
    REQUIRE(same(pixel_at(fb, 6, 0), Color{255, 255, 255, 255}));
}

TEST_CASE("texture interpolation is perspective-correct, not affine", "[rasterizer]") {
    // This is the property that separates a usable 3D texture mapper from a
    // broken one. A quad receding from the camera has a much smaller w on its
    // far edge; interpolating u linearly in screen space (affine) makes the
    // texture visibly swim as the camera moves.
    //
    // Setup: an 8x4 quad. Left edge w=1 (inv_w=1, u=0), right edge w=4
    // (inv_w=0.25, u=1).
    //
    // At pixel (4,2) -- centre (4.5, 2.5) -- the barycentric weights against
    // the triangle (0,0),(8,4),(0,4) are 0.375 / 0.5625 / 0.0625, giving
    //     interpolated 1/w = 0.578125
    //     interpolated u/w = 0.140625
    //     u = 0.140625 / 0.578125 = 0.2432  -> texel 0 -> RED
    // Affine interpolation would instead give u = 0.5625 -> texel 2 -> BLUE.
    Framebuffer fb(8, 4);
    fb.clear({0, 0, 0, 255});
    const Texture strip = make_strip_texture();

    ScreenVertex tl{0, 0, 0.5f};
    tl.u = 0.0f;
    tl.v = 0.5f;
    tl.inv_w = 1.0f;
    ScreenVertex tr{8, 0, 0.5f};
    tr.u = 1.0f;
    tr.v = 0.5f;
    tr.inv_w = 0.25f;
    ScreenVertex br{8, 4, 0.5f};
    br.u = 1.0f;
    br.v = 0.5f;
    br.inv_w = 0.25f;
    ScreenVertex bl{0, 4, 0.5f};
    bl.u = 0.0f;
    bl.v = 0.5f;
    bl.inv_w = 1.0f;

    fb.draw_triangle_textured(tl, tr, br, strip, WrapMode::Clamp, false);
    fb.draw_triangle_textured(tl, br, bl, strip, WrapMode::Clamp, false);

    const Color got = pixel_at(fb, 4, 2);
    REQUIRE(same(got, Color{255, 0, 0, 255}));        // perspective-correct
    REQUIRE_FALSE(same(got, Color{0, 0, 255, 255}));  // what affine would give
}

TEST_CASE("a fully transparent texel writes neither color nor depth", "[rasterizer]") {
    // MM6's foliage and fences are alpha-tested cutouts. A skipped texel must
    // leave the depth buffer alone, or geometry behind the cutout disappears.
    Framebuffer fb(8, 8);
    fb.clear({0, 0, 0, 255});
    const Texture clear_tex = make_solid_texture(Color{255, 255, 255, 0});
    const Texture solid = make_solid_texture(Color{0, 255, 0, 255});

    // Near, fully transparent.
    ScreenVertex a{0, 0, 0.2f};
    ScreenVertex b{8, 0, 0.2f};
    ScreenVertex c{0, 8, 0.2f};
    fb.draw_triangle_textured(a, b, c, clear_tex, WrapMode::Repeat, false);
    REQUIRE(count_filled(fb, 0) == 0);  // nothing drawn

    // Farther, opaque. It must still appear despite the nearer transparent
    // surface having covered the same pixels.
    ScreenVertex d{0, 0, 0.8f};
    ScreenVertex e{8, 0, 0.8f};
    ScreenVertex f{0, 8, 0.8f};
    fb.draw_triangle_textured(d, e, f, solid, WrapMode::Repeat, false);
    REQUIRE(same(pixel_at(fb, 1, 1), Color{0, 255, 0, 255}));
}

TEST_CASE("the vertex color modulates the sampled texel", "[rasterizer]") {
    // Lambertian terrain shading must still apply on top of a texture.
    Framebuffer fb(8, 8);
    fb.clear({0, 0, 0, 255});
    const Texture white = make_solid_texture(Color{255, 255, 255, 255});

    ScreenVertex a{0, 0, 0.5f};
    a.r = a.g = a.b = 0.5f;
    ScreenVertex b{8, 0, 0.5f};
    b.r = b.g = b.b = 0.5f;
    ScreenVertex c{0, 8, 0.5f};
    c.r = c.g = c.b = 0.5f;
    fb.draw_triangle_textured(a, b, c, white, WrapMode::Repeat, false);

    const Color got = pixel_at(fb, 1, 1);
    REQUIRE(got.r >= 126);
    REQUIRE(got.r <= 128);
    REQUIRE(got.a == 255);
}

TEST_CASE("a textured triangle respects backface culling", "[rasterizer]") {
    Framebuffer fb(8, 8);
    fb.clear({0, 0, 0, 255});
    // White, so that a drawn pixel differs from the background in every
    // channel. A texture that is black in some channel would make the
    // assertions below pass vacuously.
    const Texture solid = make_solid_texture(Color{255, 255, 255, 255});
    const Color background{0, 0, 0, 255};

    // Clockwise winding in screen space (y grows downward) = back facing.
    ScreenVertex a{0, 0, 0.5f};
    ScreenVertex b{0, 8, 0.5f};
    ScreenVertex c{8, 0, 0.5f};
    fb.draw_triangle_textured(a, b, c, solid, WrapMode::Repeat, /*cull*/ true);
    REQUIRE(same(pixel_at(fb, 1, 1), background));

    // The same winding must still draw when culling is off, which is what the
    // rasterizer's winding normalization exists to guarantee.
    fb.draw_triangle_textured(a, b, c, solid, WrapMode::Repeat, /*cull*/ false);
    REQUIRE(same(pixel_at(fb, 1, 1), Color{255, 255, 255, 255}));
}
