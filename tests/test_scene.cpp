// Tests for the shared camera and scene renderer.
//
// Everything here is synthetic geometry; no game data is involved.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

#include "core/render/scene.hpp"

using namespace starhaven::render;

TEST_CASE("a default camera looks down -Z", "[scene]") {
    Camera c;
    const Vec3 f = c.forward();
    REQUIRE(std::abs(f.x) < 1e-5f);
    REQUIRE(std::abs(f.y) < 1e-5f);
    REQUIRE(f.z < -0.99f);
}

TEST_CASE("forward_flat drops the vertical component", "[scene]") {
    Camera c;
    c.pitch = 1.0f;              // looking well upward
    const Vec3 flat = c.forward_flat();
    REQUIRE(std::abs(flat.y) < 1e-5f);
    // Still unit length in the horizontal plane, so walking speed is unaffected
    // by where the camera is looking.
    REQUIRE(std::abs(std::sqrt(flat.x*flat.x + flat.z*flat.z) - 1.0f) < 1e-5f);
}

TEST_CASE("yaw turns the camera in the horizontal plane", "[scene]") {
    Camera c;
    c.yaw = radians(90.0f);
    const Vec3 f = c.forward();
    REQUIRE(f.x > 0.99f);        // yaw 90 degrees faces +X
    REQUIRE(std::abs(f.z) < 1e-5f);
}

TEST_CASE("a point in front of the camera projects into the view", "[scene]") {
    SceneRenderer scene(640, 480);
    Camera c;
    c.position = {0, 0, 0};
    scene.begin(c, {0, 0, 0, 255});

    ScreenVertex sv;
    // Straight ahead, so it lands at the centre of the screen.
    REQUIRE(scene.project_point({0, 0, -1000}, sv));
    REQUIRE(std::abs(sv.x - 320.0f) < 1.0f);
    REQUIRE(std::abs(sv.y - 240.0f) < 1.0f);
}

TEST_CASE("a point behind the camera does not project", "[scene]") {
    SceneRenderer scene(640, 480);
    Camera c;
    scene.begin(c, {0, 0, 0, 255});
    ScreenVertex sv;
    REQUIRE_FALSE(scene.project_point({0, 0, 1000}, sv));
}

TEST_CASE("begin clears the framebuffer to the requested colour", "[scene]") {
    SceneRenderer scene(8, 4);
    Camera c;
    scene.begin(c, {10, 20, 30, 255});
    const auto px = scene.framebuffer().color();
    REQUIRE(px[0] == 10);
    REQUIRE(px[1] == 20);
    REQUIRE(px[2] == 30);
}

TEST_CASE("an untextured triangle still fills pixels", "[scene]") {
    // A missing texture must not silently drop geometry.
    SceneRenderer scene(64, 64);
    Camera c;
    scene.begin(c, {0, 0, 0, 255});

    const std::array<Vec3, 3> tri = {Vec3{-500, 500, -600}, Vec3{500, 500, -600},
                                     Vec3{0, -500, -600}};
    const std::array<Vec2, 3> uv = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    scene.draw_triangle(tri, uv, 1.0f, Texture{}, WrapMode::Clamp, false);

    const auto px = scene.framebuffer().color();
    bool any = false;
    for (std::size_t i = 0; i < px.size(); i += 4) {
        if (px[i] != 0) { any = true; break; }
    }
    REQUIRE(any);
}

TEST_CASE("a billboard with no texture draws nothing", "[scene]") {
    SceneRenderer scene(64, 64);
    Camera c;
    scene.begin(c, {0, 0, 0, 255});
    scene.draw_billboard({0, 0, -500}, 100, 100, Texture{});
    const auto px = scene.framebuffer().color();
    for (std::size_t i = 0; i < px.size(); i += 4) {
        REQUIRE(px[i] == 0);
    }
}
