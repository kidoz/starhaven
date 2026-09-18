// Tests for the static collision world.
//
// Fixtures are SYNTHETIC: polygons are built here, not read from game data.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <vector>

#include "core/render/terrain_mesh.hpp"
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

SphereSweepHit require_hit(std::optional<SphereSweepHit> hit) {
    if (!hit) {
        FAIL("expected a sphere contact");
        return {};
    }
    return *hit;
}

}  // namespace

TEST_CASE("terrain sweeps stop fast falls and recover buried centers", "[collision][terrain]") {
    OdmTerrain terrain;
    terrain.heightmap.fill(4);  // flat at 128 world units
    const CollisionWorld empty;
    const auto hit = require_hit(empty.sweep_sphere({0, 1000, 0}, {0, -1000, 0}, 16, &terrain));
    REQUIRE(hit.terrain);
    REQUIRE(hit.position.y == Catch::Approx(144));
    REQUIRE(hit.normal.y == Catch::Approx(1));
    REQUIRE(hit.fraction == Catch::Approx(856.0 / 2000));
    const auto buried = require_hit(empty.sweep_sphere({0, -200, 0}, {10, -100, 0}, 16, &terrain));
    REQUIRE(buried.fraction == 0);
    REQUIRE(buried.normal.y == 1);
    REQUIRE(buried.position.y + buried.penetration == 144);
    REQUIRE_FALSE(empty.sweep_sphere({0, 1000, 0}, {0, -1000, 0}, 16));
    REQUIRE_FALSE(empty.sweep_sphere({40000, 1000, 0}, {40000, -1000, 0}, 16, &terrain));
    REQUIRE_FALSE(empty.sweep_sphere({0, 1000, 0}, {0, 900, 0}, -1, &terrain));
    REQUIRE_FALSE(
        empty.sweep_sphere({0, std::numeric_limits<float>::infinity(), 0}, {}, 16, &terrain));
}

TEST_CASE("terrain uses rendered triangles and chooses the earlier model contact",
          "[collision][terrain]") {
    OdmTerrain terrain;
    // One non-planar cell: bilinear interpolation would put its center at
    // height 128, but the renderer's diagonal puts it at height zero.
    terrain.heightmap[64 * OdmTerrain::kGridDim + 64] = 16;
    const auto mesh = starhaven::render::build_terrain_mesh(terrain);
    CollisionWorld rendered;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const std::array vertices{
            mesh.vertices[mesh.indices[i]],
            mesh.vertices[mesh.indices[i + 1]],
            mesh.vertices[mesh.indices[i + 2]],
        };
        rendered.add_polygon(vertices, starhaven::render::cross(vertices[2] - vertices[0],
                                                                vertices[1] - vertices[0]));
    }
    CollisionWorld world;
    for (const auto target : {
             Vec3{-100, -1000, -100},
             Vec3{100, -1000, 100},
             Vec3{0, -1000, 0},
             Vec3{700, -1000, 700},
         }) {
        const Vec3 start{target.x - 1000, 1000, target.z};
        const auto actual = require_hit(world.sweep_sphere(start, target, 16, &terrain));
        const auto expected = require_hit(rendered.sweep_sphere(start, target, 16));
        REQUIRE(actual.fraction == Catch::Approx(expected.fraction));
        REQUIRE(actual.position.y == Catch::Approx(expected.position.y));
    }
    const auto diagonal = require_hit(world.sweep_sphere({0, 1000, 0}, {0, -1000, 0}, 0, &terrain));
    REQUIRE(diagonal.position.y == Catch::Approx(0));
    const std::array platform{
        Vec3{-100, 300, -100},
        Vec3{100, 300, -100},
        Vec3{100, 300, 100},
        Vec3{-100, 300, 100},
    };
    world.add_polygon(platform, {0, 1, 0});
    const auto platform_hit =
        require_hit(world.sweep_sphere({0, 1000, 0}, {0, -1000, 0}, 16, &terrain));
    REQUIRE_FALSE(platform_hit.terrain);
    REQUIRE(platform_hit.position.y == Catch::Approx(316));
    const auto ground = require_hit(world.sweep_sphere({0, 200, 0}, {0, -1000, 0}, 0, &terrain));
    REQUIRE(ground.terrain);
    REQUIRE(ground.position.y == Catch::Approx(0));
}

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

TEST_CASE("sphere sweeps stop at the earliest wall from either side", "[collision][sweep]") {
    CollisionWorld world;
    add_wall(world, 80.0f);
    add_wall(world, 20.0f);  // nearest is deliberately inserted last
    const auto front = require_hit(world.sweep_sphere({0, 5, 0}, {0, 5, 100}, 3));
    CHECK(front.fraction == Catch::Approx(0.17f));
    CHECK(front.position.z == Catch::Approx(17.0f));
    CHECK(front.normal.z == Catch::Approx(-1.0f));
    CHECK(front.penetration == 0.0f);
    const auto back = require_hit(world.sweep_sphere({0, 5, 100}, {0, 5, 0}, 3));
    CHECK(back.fraction == Catch::Approx(0.17f));
    CHECK(back.position.z == Catch::Approx(83.0f));
    CHECK(back.normal.z == Catch::Approx(1.0f));
}

TEST_CASE("sphere sweeps collide with floors and ceilings", "[collision][sweep]") {
    CollisionWorld world;
    add_floor(world, 0);
    add_floor(world, 20);
    const auto floor = require_hit(world.sweep_sphere({0, 10, 0}, {0, -10, 0}, 2));
    CHECK(floor.fraction == Catch::Approx(0.4f));
    CHECK(floor.position.y == Catch::Approx(2.0f));
    CHECK(floor.normal.y == Catch::Approx(1.0f));
    const auto ceiling = require_hit(world.sweep_sphere({0, 10, 0}, {0, 30, 0}, 2));
    CHECK(ceiling.fraction == Catch::Approx(0.4f));
    CHECK(ceiling.position.y == Catch::Approx(18.0f));
    CHECK(ceiling.normal.y == Catch::Approx(-1.0f));
}

TEST_CASE("sphere sweeps do not tunnel during a long movement", "[collision][sweep]") {
    CollisionWorld world;
    add_wall(world, 0);
    const auto hit = require_hit(world.sweep_sphere({0, 5, -1000000}, {0, 5, 1000000}, 2));
    CHECK(hit.position.z == Catch::Approx(-2.0f));
    CHECK(hit.normal.z == Catch::Approx(-1.0f));
    // Double intermediates also keep finite float endpoints from overflowing
    // the displacement or squared-distance calculations.
    const float limit = std::numeric_limits<float>::max();
    const auto enormous = require_hit(world.sweep_sphere({0, 5, -limit}, {0, 5, limit}, 2));
    CHECK(std::isfinite(enormous.position.z));
    CHECK(enormous.fraction == Catch::Approx(0.5f));
    CHECK(enormous.normal.z == Catch::Approx(-1.0f));
}

TEST_CASE("sphere sweeps detect rounded edges and vertices beyond the face", "[collision][sweep]") {
    CollisionWorld world;
    add_wall(world, 0, 10);
    const auto edge = require_hit(world.sweep_sphere({11, 5, -5}, {11, 5, 5}, 2));
    CHECK(edge.position.z == Catch::Approx(-std::sqrt(3.0f)));
    CHECK(edge.normal.x == Catch::Approx(0.5f));
    CHECK(edge.normal.z == Catch::Approx(-std::sqrt(3.0f) / 2.0f));
    const auto vertex = require_hit(world.sweep_sphere({11, 11, -5}, {11, 11, 5}, 2));
    CHECK(vertex.position.z == Catch::Approx(-std::sqrt(2.0f)));
    CHECK(vertex.normal.x == Catch::Approx(0.5f));
    CHECK(vertex.normal.y == Catch::Approx(0.5f));
    CHECK(vertex.normal.z == Catch::Approx(-std::sqrt(2.0f) / 2.0f));
    const auto parallel = require_hit(world.sweep_sphere({15, 5, 1}, {0, 5, 1}, 2));
    CHECK(parallel.position.x == Catch::Approx(10.0f + std::sqrt(3.0f)));
    CHECK(parallel.normal.z == Catch::Approx(0.5f));
    const auto tangent = require_hit(world.sweep_sphere({12, 10, -5}, {12, 10, 5}, 2));
    CHECK(tangent.fraction == Catch::Approx(0.5f));
    CHECK(tangent.normal.x == Catch::Approx(1.0f));
    CHECK_FALSE(world.sweep_sphere({12.01f, 10, -5}, {12.01f, 10, 5}, 2));
}

TEST_CASE("sphere sweeps preserve polygon winding independence and sloped normals",
          "[collision][sweep]") {
    const std::array<Vec3, 4> slope{{{-10, -10, -10}, {10, 10, -10}, {10, 10, 10}, {-10, -10, 10}}};
    for (const float facing : {-1.0f, 1.0f}) {
        CAPTURE(facing);
        CollisionWorld world;
        world.add_polygon(slope, {-facing, facing, 0});
        const auto hit = require_hit(world.sweep_sphere({0, 10, 0}, {0, -10, 0}, 2));
        CHECK(hit.position.y == Catch::Approx(2.0f * std::sqrt(2.0f)));
        CHECK(hit.normal.x == Catch::Approx(-1.0f / std::sqrt(2.0f)));
        CHECK(hit.normal.y == Catch::Approx(1.0f / std::sqrt(2.0f)));
    }
}

TEST_CASE("sphere sweeps distinguish misses from endpoint and zero-radius contacts",
          "[collision][sweep]") {
    CollisionWorld world;
    add_wall(world, 0, 10);
    CHECK_FALSE(CollisionWorld{}.sweep_sphere({0, 5, -5}, {0, 5, 5}, 2));
    CHECK_FALSE(world.sweep_sphere({20, 5, -5}, {20, 5, 5}, 2));
    CHECK_FALSE(world.sweep_sphere({0, 5, -5}, {0, 5, -3}, 2));
    const auto endpoint = require_hit(world.sweep_sphere({0, 5, -5}, {0, 5, -2}, 2));
    CHECK(endpoint.fraction == Catch::Approx(1.0f));
    for (const Vec3 point : {Vec3{0, 5, 0}, Vec3{10, 5, 0}, Vec3{10, 10, 0}}) {
        const auto hit =
            require_hit(world.sweep_sphere({point.x, point.y, -5}, {point.x, point.y, 5}, 0));
        CHECK(hit.fraction == Catch::Approx(0.5f));
        CHECK(hit.position.z == Catch::Approx(0.0f));
        CHECK(hit.normal.z == Catch::Approx(-1.0f));
    }
    const auto diagonal = require_hit(world.sweep_sphere({12, 13, -5}, {8, 7, 5}, 0));
    CHECK(diagonal.fraction == Catch::Approx(0.5f));
    CHECK(diagonal.position.x == Catch::Approx(10.0f));
    CHECK(diagonal.position.y == Catch::Approx(10.0f));
}

TEST_CASE("sphere sweep initial contacts are deterministic including zero movement",
          "[collision][sweep]") {
    CollisionWorld world;
    add_floor(world, 0);
    for (const Vec3 destination : {Vec3{0, 1, 0}, Vec3{0, 10, 0}, Vec3{0, -10, 0}}) {
        const auto hit = require_hit(world.sweep_sphere({0, 1, 0}, destination, 2));
        CHECK(hit.fraction == 0.0f);
        CHECK(hit.position.y == 1.0f);
        CHECK(hit.normal.y == 1.0f);
        CHECK(hit.penetration == 1.0f);
    }
    CHECK_FALSE(world.sweep_sphere({0, 3, 0}, {0, 3, 0}, 2));
    CHECK(require_hit(world.sweep_sphere({0, 2, 0}, {0, 3, 0}, 2)).fraction == 0.0f);
    const auto centered = require_hit(world.sweep_sphere({0, 0, 0}, {0, 1, 0}, 0));
    CHECK(centered.normal.y == -1.0f);
    CHECK(centered.penetration == 0.0f);
    const auto still = require_hit(world.sweep_sphere({0, 0, 0}, {0, 0, 0}, 0));
    CHECK(still.normal.y == 1.0f);
    add_wall(world, 0);
    const auto tied = require_hit(world.sweep_sphere({0, 0, 0}, {0, 0, 0}, 1));
    CHECK(tied.normal.y == 1.0f);  // first inserted polygon wins
    CollisionWorld edge_world;
    add_wall(edge_world, 0, 10);
    const auto edge = require_hit(edge_world.sweep_sphere({11, 5, 0}, {20, 5, 0}, 2));
    CHECK(edge.fraction == 0.0f);
    CHECK(edge.normal.x == 1.0f);
    CHECK(edge.penetration == 1.0f);
}

TEST_CASE("deep sphere overlaps report a complete separation from the chosen surface",
          "[collision][sweep]") {
    CollisionWorld world;
    add_floor(world, 0);
    const auto embedded = require_hit(world.sweep_sphere({0, 0.5f, 0}, {0, 10, 0}, 4));
    CHECK(embedded.fraction == 0.0f);
    CHECK(embedded.penetration == Catch::Approx(3.5f));
    CHECK(embedded.position.y == 0.5f);
    const auto separated = embedded.position + embedded.normal * (embedded.penetration + 0.01f);
    CHECK_FALSE(world.sweep_sphere(separated, {0, 10, 0}, 4));
    const auto centered = require_hit(world.sweep_sphere({0, 0, 0}, {0, -10, 0}, 4));
    CHECK(centered.penetration == 4.0f);
    CHECK(centered.normal.y == 1.0f);
}

TEST_CASE("sphere sweeps reject invalid input and polygons", "[collision][sweep]") {
    CollisionWorld world;
    add_floor(world, 0);
    CHECK_FALSE(world.sweep_sphere({0, 10, 0}, {0, -10, 0}, -1));
    for (const float invalid : {
             std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN(),
         }) {
        CHECK_FALSE(world.sweep_sphere({invalid, 10, 0}, {0, -10, 0}, 1));
        CHECK_FALSE(world.sweep_sphere({0, 10, 0}, {0, invalid, 0}, 1));
        CHECK_FALSE(world.sweep_sphere({0, 10, 0}, {0, -10, 0}, invalid));
        const std::array<Vec3, 3> vertices{{{0, 0, 0}, {1, 0, 0}, {0, 0, invalid}}};
        world.add_polygon(vertices, {0, 1, 0});
        const std::array<Vec3, 3> valid{{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}};
        world.add_polygon(valid, {0, invalid, 0});
    }
    CHECK(world.size() == 1);
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
