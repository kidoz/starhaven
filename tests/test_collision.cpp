// Tests for the static collision world.
//
// Fixtures are SYNTHETIC: polygons are built here, not read from game data.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "core/world/collision.hpp"
#include "game/player.hpp"

using namespace starhaven::world;
using starhaven::render::Vec3;

namespace {

// A horizontal square at height y, spanning [-size, size] in x and z.
void add_floor(CollisionWorld& w, float y, float size = 100.0f) {
    const std::array<Vec3, 4> quad = {Vec3{-size, y, -size}, Vec3{size, y, -size},
                                      Vec3{size, y, size}, Vec3{-size, y, size}};
    w.add_polygon(quad, {0, 1, 0});
}

// A vertical wall in the z = `at` plane, facing -z, spanning x/y.
void add_wall(CollisionWorld& w, float at, float size = 100.0f) {
    const std::array<Vec3, 4> quad = {Vec3{-size, 0, at}, Vec3{size, 0, at}, Vec3{size, size, at},
                                      Vec3{-size, size, at}};
    w.add_polygon(quad, {0, 0, -1});
}

}  // namespace

TEST_CASE("a degenerate polygon is ignored", "[collision]") {
    CollisionWorld w;
    const std::array<Vec3, 2> line = {Vec3{0, 0, 0}, Vec3{1, 0, 0}};
    w.add_polygon(line, {0, 1, 0});
    const std::array<Vec3, 3> tri = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}};
    w.add_polygon(tri, {0, 0, 0});  // zero-length normal
    REQUIRE(w.size() == 0);
}

TEST_CASE("the floor under a point is found", "[collision]") {
    CollisionWorld w;
    add_floor(w, 64.0f);

    float y = 0;
    REQUIRE(w.floor_below({0, 500, 0}, y));
    REQUIRE(y == 64.0f);
}

TEST_CASE("the highest floor below the player wins", "[collision]") {
    // A walkway over a lower floor: standing on the walkway must not drop the
    // player through it.
    CollisionWorld w;
    add_floor(w, 0.0f);
    add_floor(w, 200.0f, 50.0f);

    float y = 0;
    REQUIRE(w.floor_below({0, 500, 0}, y));
    REQUIRE(y == 200.0f);

    // Beyond the walkway's edge only the lower floor is underfoot.
    REQUIRE(w.floor_below({80, 500, 0}, y));
    REQUIRE(y == 0.0f);
}

TEST_CASE("a floor above the player is not stood on", "[collision]") {
    CollisionWorld w;
    add_floor(w, 400.0f);
    float y = 0;
    REQUIRE_FALSE(w.floor_below({0, 100, 0}, y));
}

TEST_CASE("nothing underfoot is reported, not invented", "[collision]") {
    CollisionWorld w;
    add_floor(w, 0.0f, 10.0f);
    float y = 0;
    REQUIRE_FALSE(w.floor_below({500, 100, 0}, y));  // past the floor's edge
}

TEST_CASE("walls are not treated as floor", "[collision]") {
    CollisionWorld w;
    add_wall(w, 0.0f);
    float y = 0;
    REQUIRE_FALSE(w.floor_below({0, 500, 0}, y));
}

TEST_CASE("walking into a wall stops at the wall", "[collision]") {
    CollisionWorld w;
    add_wall(w, 100.0f);

    const Vec3 from{0, 0, 0};
    const Vec3 to{0, 0, 120};  // straight through the wall
    const Vec3 out = w.slide(from, to, /*radius*/ 20.0f, /*height*/ 60.0f);
    REQUIRE(out.z <= 80.0f + 0.01f);  // pushed back to radius from the plane
    REQUIRE(out.z >= 80.0f - 0.01f);
}

TEST_CASE("moving at an angle slides along the wall", "[collision]") {
    // The push is along the wall normal, so motion parallel to the wall is
    // preserved rather than cancelled.
    CollisionWorld w;
    add_wall(w, 100.0f);

    const Vec3 out = w.slide({0, 0, 0}, {50, 0, 120}, 20.0f, 60.0f);
    REQUIRE(out.x == 50.0f);          // sideways motion kept
    REQUIRE(out.z <= 80.0f + 0.01f);  // forward motion clamped
}

TEST_CASE("a wall the player is clear of does not push", "[collision]") {
    CollisionWorld w;
    add_wall(w, 100.0f);
    const Vec3 to{0, 0, 10};
    const Vec3 out = w.slide({0, 0, 0}, to, 20.0f, 60.0f);
    REQUIRE(out.z == to.z);
}

TEST_CASE("a wall that does not extend to the player does not push", "[collision]") {
    // The plane is infinite but the polygon is not; a wall off to one side
    // must not block movement.
    CollisionWorld w;
    add_wall(w, 100.0f, 10.0f);  // only spans x in [-10, 10]
    const Vec3 to{500, 0, 120};
    const Vec3 out = w.slide({500, 0, 0}, to, 20.0f, 60.0f);
    REQUIRE(out.z == to.z);
}

TEST_CASE("point_in_polygon respects the polygon's bounds", "[collision]") {
    CollisionWorld w;
    add_floor(w, 0.0f, 100.0f);
    const auto& poly = w.polygons().front();
    REQUIRE(point_in_polygon(poly, {0, 0, 0}));
    REQUIRE(point_in_polygon(poly, {99, 0, 99}));
    REQUIRE_FALSE(point_in_polygon(poly, {101, 0, 0}));
    REQUIRE_FALSE(point_in_polygon(poly, {0, 0, -101}));
}

// --- the movement step -----------------------------------------------------

namespace {

using starhaven::game::MoveInput;
using starhaven::game::step_player;

// A camera standing with its feet at `y` over flat ground of height `ground`.
starhaven::render::Camera standing(float x, float y, float z) {
    starhaven::render::Camera c;
    c.position = {x, y + starhaven::game::kEyeHeight, z};
    return c;
}

float feet_of(const starhaven::render::Camera& c) {
    return c.position.y - starhaven::game::kEyeHeight;
}

MoveInput forward(float speed = 400.0f) {
    MoveInput in;
    in.forward = true;
    in.speed = speed;
    return in;
}

}  // namespace

TEST_CASE("a cliff is not climbable", "[player]") {
    // Terrain is sampled, not collided. Without a rise limit the player is
    // snapped to whatever height the destination has, so a vertical cliff
    // teleports them to the top of it.
    const CollisionWorld empty;
    auto cliff = [](float x, float) { return x > 100.0f ? 5000.0f : 0.0f; };

    auto camera = standing(0, 0, 0);
    // yaw 0 looks down -z; a right angle turns the walk onto +x.
    camera.yaw = 1.5707963f;
    float fall = 0.0f;
    for (int i = 0; i < 120; ++i) {
        step_player(camera, fall, false, forward(), empty, cliff);
    }
    REQUIRE(camera.position.x <= 100.0f);
    REQUIRE(feet_of(camera) < 1000.0f);
}

TEST_CASE("a step is walked up", "[player]") {
    // The same terrain shape, but a rise the player's legs can manage.
    const CollisionWorld empty;
    auto step = [](float x, float) { return x > 100.0f ? 64.0f : 0.0f; };

    auto camera = standing(0, 0, 0);
    camera.yaw = 1.5707963f;  // +x
    float fall = 0.0f;
    for (int i = 0; i < 120; ++i) {
        step_player(camera, fall, false, forward(), empty, step);
    }
    REQUIRE(camera.position.x > 200.0f);
    REQUIRE(feet_of(camera) > 63.0f);
}

TEST_CASE("a cliff can be walked along, not only away from", "[player]") {
    // Refusing the whole move would pin the player against the cliff; the
    // move is retried on each axis so the sideways component survives.
    const CollisionWorld empty;
    auto cliff = [](float x, float) { return x > 100.0f ? 5000.0f : 0.0f; };

    auto camera = standing(0, 0, 0);
    camera.yaw = 1.5707963f - 0.7f;  // diagonally into the cliff and along it
    float fall = 0.0f;
    for (int i = 0; i < 60; ++i) {
        step_player(camera, fall, false, forward(), empty, cliff);
    }
    REQUIRE(camera.position.x <= 100.0f);
    REQUIRE(std::abs(camera.position.z) > 50.0f);
}

TEST_CASE("the step up is smoothed, not snapped", "[player]") {
    const CollisionWorld empty;
    // One frame at 400 units per second covers about 6.7 units, so the step
    // has to start inside that for the first frame to land on it.
    auto step = [](float x, float) { return x > 5.0f ? 96.0f : 0.0f; };

    auto camera = standing(0, 0, 0);
    camera.yaw = 1.5707963f;  // +x
    float fall = 0.0f;
    // One frame is enough to cross onto the step at this speed.
    step_player(camera, fall, false, forward(), empty, step);
    const float after_one = feet_of(camera);
    REQUIRE(after_one > 0.0f);
    REQUIRE(after_one < 96.0f);  // still climbing

    for (int i = 0; i < 30; ++i) {
        step_player(camera, fall, false, forward(), empty, step);
    }
    REQUIRE(feet_of(camera) > 95.0f);  // arrived
}

TEST_CASE("a fast step does not pass through a thin wall", "[player]") {
    // A single end-point test lets a long step straddle a wall entirely. The
    // body is swept in pieces so the wall is met on the way.
    CollisionWorld world;
    const std::vector<starhaven::render::Vec3> wall{
        {200, -1000, -1000}, {200, 1000, -1000}, {200, 1000, 1000}, {200, -1000, 1000}};
    world.add_polygon(wall, {-1, 0, 0});

    auto flat = [](float, float) { return 0.0f; };
    auto camera = standing(0, 0, 0);
    camera.yaw = 1.5707963f;  // +x
    float fall = 0.0f;
    // 20,000 units per second for a frame: far past the wall in one step.
    step_player(camera, fall, false, forward(20000.0f), world, flat);
    REQUIRE(camera.position.x < 200.0f);
}
