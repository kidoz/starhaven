// Tests for terrain mesh construction from an ODM heightmap.
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

#include "core/render/terrain_mesh.hpp"
#include "core/world/odm_map.hpp"

using namespace starhaven;
using namespace starhaven::render;

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

TEST_CASE("the default scale spans the full 2^16 MM6 world", "[terrain_mesh]") {
    // Calibration guard. cell_size is not a free parameter: the ODM stores
    // model positions in real world units, and 512 is the smallest value that
    // places every model in Outa1.odm inside the grid. It also makes the map
    // exactly 128 * 512 = 65536 units across. An 8x-too-small cell_size (the
    // previous 64) compressed the terrain horizontally and rendered rolling
    // hills as near-vertical spikes.
    constexpr int dim = world::OdmTerrain::kGridDim;
    const TerrainScale s{};
    REQUIRE(s.cell_size == Approx(512.0f));
    REQUIRE(static_cast<float>(dim) * s.cell_size == Approx(65536.0f));

    auto mesh = build_terrain_mesh(flat_terrain(), {});
    float min_x = mesh.vertices[0].x, max_x = mesh.vertices[0].x;
    for (const Vec3& v : mesh.vertices) {
        min_x = std::min(min_x, v.x);
        max_x = std::max(max_x, v.x);
    }
    // (dim-1) cells between the first and last vertex.
    REQUIRE(max_x - min_x == Approx(static_cast<float>(dim - 1) * 512.0f));
}

TEST_CASE("triangles wind CW from above so screen-space culling keeps them",
          "[terrain_mesh]") {
    // Regression guard. Projection flips Y when mapping NDC to screen rows,
    // and that flip reverses orientation. Emitting world-space CCW here makes
    // every top face arrive at the rasterizer with negative screen area, so
    // backface culling discards the whole terrain and the view is empty sky.
    auto mesh = build_terrain_mesh(flat_terrain(), {});
    REQUIRE(mesh.indices.size() >= 3);

    const Vec3 a = mesh.vertices[mesh.indices[0]];
    const Vec3 b = mesh.vertices[mesh.indices[1]];
    const Vec3 c = mesh.vertices[mesh.indices[2]];

    // Signed area in the ground plane, looking down +Y. Positive here is the
    // winding that survives culling once Y is flipped downstream.
    const float area = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    REQUIRE(area > 0.0f);
}

TEST_CASE("mesh carries one uv per vertex, in cell units", "[terrain_mesh]") {
    auto mesh = build_terrain_mesh(flat_terrain(), {});
    REQUIRE(mesh.uvs.size() == mesh.vertices.size());

    constexpr int dim = world::OdmTerrain::kGridDim;
    // The vertex at grid (x, y) carries uv (x, y).
    REQUIRE(mesh.uvs[0].u == Approx(0.0f));
    REQUIRE(mesh.uvs[0].v == Approx(0.0f));
    REQUIRE(mesh.uvs[1].u == Approx(1.0f));
    REQUIRE(mesh.uvs[1].v == Approx(0.0f));
    REQUIRE(mesh.uvs[static_cast<std::size_t>(dim)].u == Approx(0.0f));
    REQUIRE(mesh.uvs[static_cast<std::size_t>(dim)].v == Approx(1.0f));
}

TEST_CASE("each cell spans exactly one uv unit so Repeat lays one tile per cell",
          "[terrain_mesh]") {
    // This is the property that makes shared vertices work: no per-vertex
    // assignment can give each cell its own 0..1 span, but in cell units the
    // *difference* across any cell is exactly 1.
    auto mesh = build_terrain_mesh(flat_terrain(), {});
    constexpr int dim = world::OdmTerrain::kGridDim;

    const auto at = [&](int x, int y) {
        return mesh.uvs[static_cast<std::size_t>(y) * dim + x];
    };
    for (int cell : {0, 1, 57, dim - 2}) {
        REQUIRE(at(cell + 1, 0).u - at(cell, 0).u == Approx(1.0f));
        REQUIRE(at(0, cell + 1).v - at(0, cell).v == Approx(1.0f));
    }
}

TEST_CASE("mesh carries one tile id per triangle, two per cell",
          "[terrain_mesh]") {
    world::OdmTerrain t = flat_terrain();
    constexpr int dim = world::OdmTerrain::kGridDim;
    // Give cell (0,0) and cell (3,2) distinctive tile indices.
    t.tilemap[0] = 90;
    t.tilemap[static_cast<std::size_t>(2) * dim + 3] = 190;

    auto mesh = build_terrain_mesh(t, {});
    REQUIRE(mesh.tile_ids.size() == mesh.indices.size() / 3);

    // Cells are emitted row-major, two triangles each.
    const auto cell_tri = [&](int x, int y) {
        return static_cast<std::size_t>(y) * (dim - 1) * 2 +
               static_cast<std::size_t>(x) * 2;
    };
    REQUIRE(mesh.tile_ids[cell_tri(0, 0)] == 90);
    REQUIRE(mesh.tile_ids[cell_tri(0, 0) + 1] == 90);  // both triangles agree
    REQUIRE(mesh.tile_ids[cell_tri(3, 2)] == 190);
    REQUIRE(mesh.tile_ids[cell_tri(3, 2) + 1] == 190);
    // An untouched cell keeps the fill value.
    REQUIRE(mesh.tile_ids[cell_tri(5, 5)] == 0);
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
