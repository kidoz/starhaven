// Tests for the software rasterizer.
#include <catch2/catch_test_macros.hpp>

#include "core/render/rasterizer.hpp"

using namespace openmm6::render;

namespace {

// Helper: count filled (non-background) pixels in a framebuffer.
int count_filled(const Framebuffer& fb, std::uint8_t bg) {
    int n = 0;
    auto c = fb.color();
    for (int i = 0; i < fb.width() * fb.height(); ++i) {
        if (c[i * 4] != bg) ++n;
    }
    return n;
}

bool is_filled(const Framebuffer& fb, int x, int y) {
    return fb.color()[(y * fb.width() + x) * 4] != 0;
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

TEST_CASE("near-plane clipping splits a straddling triangle into a quad (6 verts)", "[rasterizer]") {
    // Two vertices in front (z=-5), one behind (z=-0.5).
    ViewVertex tri[3] = {{0, 0, -5, 1}, {1, 0, -5, 1}, {0, 1, -0.5f, 1}};
    std::vector<ViewVertex> out;
    clip_near(tri, /*near_z*/ -1.0f, out);
    REQUIRE(out.size() == 6);  // a quad -> two triangles
}
