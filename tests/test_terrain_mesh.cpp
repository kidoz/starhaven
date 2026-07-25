// Tests for terrain mesh construction from an ODM heightmap.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

#include "core/render/terrain_mesh.hpp"
#include "core/world/odm_map.hpp"

using namespace openmm6;
using namespace openmm6::render;

namespace {

world::OdmTerrain flat_terrain(std::uint8_t h = 10) {
    world::OdmTerrain t;
    t.heightmap.fill(h);
    t.tilemap.fill(0);
    return t;
}

}  // namespace

TEST_CASE("terrain mesh vertex/triangle counts", "[terrain_mesh]") {
    auto mesh = build_terrain_mesh(flat_terrain(), {});
    constexpr int dim = world::OdmTerrain::kGridDim;  // 128
    REQUIRE(mesh.vertices.size() == static_cast<std::size_t>(dim) * dim);
    REQUIRE(mesh.normals.size() == mesh.vertices.size());
    REQUIRE(mesh.indices.size() ==
            static_cast<std::size_t>(dim - 1) * (dim - 1) * 6);
}

TEST_CASE("flat terrain has upward normals", "[terrain_mesh]") {
    auto mesh = build_terrain_mesh(flat_terrain(50), {});
    // A flat field: every normal points straight up (+Y).
    for (const auto& n : mesh.normals) {
        REQUIRE(n.y == Approx(1.0f).margin(1e-4f));
        REQUIRE(std::abs(n.x) < 1e-4f);
        REQUIRE(std::abs(n.z) < 1e-4f);
    }
}

TEST_CASE("mesh is centered on the origin", "[terrain_mesh]") {
    auto mesh = build_terrain_mesh(flat_terrain(), TerrainScale{64.0f, 32.0f});
    // The (0,0) corner and the (127,127) corner are symmetric about the origin
    // in X and Z (both flat in Y).
    const auto& v00 = mesh.vertices.front();
    const auto& vNN = mesh.vertices.back();
    REQUIRE(v00.x == Approx(-vNN.x).margin(1e-3f));
    REQUIRE(v00.z == Approx(-vNN.z).margin(1e-3f));
    REQUIRE(v00.y == Approx(vNN.y).margin(1e-3f));
}

TEST_CASE("height scale is applied to vertex Y", "[terrain_mesh]") {
    auto mesh = build_terrain_mesh(flat_terrain(20), TerrainScale{1.0f, 5.0f});
    // Every vertex Y = height * height_scale = 20 * 5 = 100.
    for (const auto& v : mesh.vertices) {
        REQUIRE(v.y == Approx(100.0f).margin(1e-3f));
    }
}

TEST_CASE("a height step produces a tilted normal", "[terrain_mesh]") {
    world::OdmTerrain t = flat_terrain(10);
    // Raise one column so the X-slope is upward toward +X.
    constexpr int dim = world::OdmTerrain::kGridDim;
    for (int y = 0; y < dim; ++y) {
        t.heightmap[y * dim + (dim / 2)] = 100;
    }
    auto mesh = build_terrain_mesh(t, TerrainScale{1.0f, 1.0f});
    // A vertex immediately left of the ridge should have a normal leaning -X
    // (away from the higher ground). Compare its X component against a flat
    // field's normal (which is 0) to avoid the -0.0 == 0 pitfall.
    const int x = dim / 2 - 1;
    const int y = dim / 2;
    const Vec3& n = mesh.normals[y * dim + x];
    REQUIRE(n.x < -0.01f);   // clearly negative, leaning away from the ridge
    REQUIRE(n.y > 0.0f);     // still pointing up
}

// --- terrain coloring tests ------------------------------------------------

TEST_CASE("base grass tile gets the green palette color", "[terrain_mesh]") {
    Vec3Color c = tile_type_color(90);
    REQUIRE(c.g > c.r);
    REQUIRE(c.g > c.b);
}

TEST_CASE("water-range tiles get the blue palette color", "[terrain_mesh]") {
    Vec3Color c = tile_type_color(200);
    REQUIRE(c.b > c.r);
    REQUIRE(c.b > c.g);
}

TEST_CASE("tile type color is stable for the same index", "[terrain_mesh]") {
    Vec3Color a = tile_type_color(77);
    Vec3Color b = tile_type_color(77);
    REQUIRE(a.r == b.r);
    REQUIRE(a.g == b.g);
    REQUIRE(a.b == b.b);
}

TEST_CASE("distinct tile indices get distinct colors", "[terrain_mesh]") {
    Vec3Color a = tile_type_color(50);
    Vec3Color b = tile_type_color(120);
    REQUIRE((a.r != b.r || a.g != b.g || a.b != b.b));
}

TEST_CASE("build_terrain_colors yields one color per vertex", "[terrain_mesh]") {
    auto t = flat_terrain(0);
    constexpr int dim = world::OdmTerrain::kGridDim;
    // Stamp two distinct tile types.
    t.tilemap[0] = 90;
    t.tilemap[dim * dim - 1] = 200;
    auto colors = build_terrain_colors(t);
    REQUIRE(colors.size() == static_cast<std::size_t>(dim) * dim);
    REQUIRE(colors[0].g > colors[0].b);              // grass
    REQUIRE(colors[colors.size() - 1].b > colors[colors.size() - 1].g);  // water
}
