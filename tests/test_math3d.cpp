// Tests for the 3D math helpers.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

#include <cmath>

#include "core/render/math3d.hpp"

using namespace starhaven::render;

constexpr float kEps = 1e-5f;

TEST_CASE("vec3 dot/cross/normalize", "[math3d]") {
    REQUIRE(dot({1, 2, 3}, {4, 5, 6}) == 32.0f);
    Vec3 c = cross({1, 0, 0}, {0, 1, 0});
    REQUIRE(c.x == 0);
    REQUIRE(c.y == 0);
    REQUIRE(c.z == 1);
    Vec3 n = normalize({0, 0, 5});
    REQUIRE(n.z == 1.0f);
    REQUIRE(length(normalize({3, 4, 0})) == 1.0f);
}

TEST_CASE("mat4 * vec4 identity and translate", "[math3d]") {
    Vec4 v = mat4_identity() * Vec4{1, 2, 3, 1};
    REQUIRE(v.x == 1);
    REQUIRE(v.y == 2);
    REQUIRE(v.z == 3);
    REQUIRE(v.w == 1);
    Vec4 t = mat4_translate({10, 20, 30}) * Vec4{1, 2, 3, 1};
    REQUIRE(t.x == 11);
    REQUIRE(t.y == 22);
    REQUIRE(t.z == 33);
}

TEST_CASE("rotation_y rotates a point 90 degrees about Y", "[math3d]") {
    // +X rotated 90deg about Y (CCW viewed from above) -> -Z.
    Vec4 r = mat4_rotation_y(kPi / 2) * Vec4{1, 0, 0, 1};
    REQUIRE(std::abs(r.x) < kEps);
    REQUIRE(std::abs(r.y) < kEps);
    REQUIRE(r.z == Approx(-1.0f).margin(kEps));
}

TEST_CASE("perspective maps view-space to NDC z in [0,1]", "[math3d]") {
    Mat4 p = mat4_perspective(kPi / 2, 1.0f, 1.0f, 100.0f);
    // A point at z=-5 (in front) maps to NDC z in (0,1).
    Vec4 a = p * Vec4{0, 0, -5, 1};
    const float inv_w_a = 1.0f / a.w;
    float ndc_z_a = a.z * inv_w_a;
    REQUIRE(ndc_z_a > 0.0f);
    REQUIRE(ndc_z_a < 1.0f);
    // A farther point (z=-50) maps to a larger NDC z.
    Vec4 b = p * Vec4{0, 0, -50, 1};
    float ndc_z_b = b.z * (1.0f / b.w);
    REQUIRE(ndc_z_b > ndc_z_a);
    REQUIRE(ndc_z_b < 1.0f);
}

TEST_CASE("perspective at the near plane maps to NDC z=0", "[math3d]") {
    Mat4 p = mat4_perspective(kPi / 2, 1.0f, 1.0f, 100.0f);
    Vec4 a = p * Vec4{0, 0, -1, 1};
    REQUIRE(std::abs(a.z * (1.0f / a.w)) < kEps);
}

TEST_CASE("look_at: camera origin maps to origin", "[math3d]") {
    // The eye point, transformed by the view matrix, should land at the origin.
    Mat4 v = mat4_look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    Vec4 e = v * Vec4{0, 0, 5, 1};
    REQUIRE(std::abs(e.x) < kEps);
    REQUIRE(std::abs(e.y) < kEps);
    REQUIRE(std::abs(e.z) < kEps);
}

TEST_CASE("camera_forward/right at yaw 0", "[math3d]") {
    Vec3 f = camera_forward(0, 0);
    REQUIRE(f.x == 0);
    REQUIRE(f.y == 0);
    REQUIRE(f.z == Approx(-1).margin(kEps));
    Vec3 r = camera_right(0);
    REQUIRE(r.x == Approx(1).margin(kEps));
    REQUIRE(r.z == 0);
}

TEST_CASE("matrix multiply is associative with identity", "[math3d]") {
    Mat4 t = mat4_translate({1, 2, 3});
    Mat4 prod = mat4_identity() * t;
    Vec4 v = prod * Vec4{0, 0, 0, 1};
    REQUIRE(v.x == 1);
    REQUIRE(v.y == 2);
    REQUIRE(v.z == 3);
}
