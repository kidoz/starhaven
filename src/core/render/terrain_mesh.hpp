#ifndef STARHAVEN_CORE_RENDER_TERRAIN_MESH_HPP
#define STARHAVEN_CORE_RENDER_TERRAIN_MESH_HPP

#include <cstdint>
#include <vector>

#include "core/render/math3d.hpp"
#include "core/world/odm_map.hpp"

namespace starhaven::render {

// A terrain mesh built from an ODM heightmap. Vertices are positioned in world
// space as (cell_x * cell_size, height * height_scale, cell_y * cell_size),
// centered on the origin so the camera starts over the map middle.
struct TerrainMesh {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;  // per-vertex, normalized
    std::vector<std::uint32_t> indices;  // triangles: 3 indices each

    // Per-vertex texture coordinates in *cell* units: the vertex at grid
    // (x, y) gets uv (x, y).
    //
    // Terrain vertices are shared between adjacent cells, so no per-vertex
    // assignment can give each cell its own 0..1 span. Cell units sidestep
    // that: within any one cell the corners span exactly 1.0 in each axis, so
    // sampling with WrapMode::Repeat lays one full tile across every cell
    // while the vertices stay shared.
    std::vector<Vec2> uvs;

    // The tilemap index of the cell each triangle belongs to; one entry per
    // triangle, so `tile_ids.size() == indices.size() / 3`. Both triangles of
    // a cell carry the same id.
    //
    // Stored explicitly rather than recomputed as `triangle / 2` so that the
    // mapping survives any future change to triangulation order.
    std::vector<std::uint8_t> tile_ids;
};

// World-scale defaults. The MM6 grid is 128x128; one horizontal cell maps to
// `cell_size` world units and one height unit to `height_scale` world units.
//
// cell_size is calibrated against model placement, which the ODM stores in
// real world units: 512 is the smallest value that puts all of Outa1.odm's 23
// models inside the grid (64, 128 and 256 leave 23, 22 and 3 of them off the
// map respectively), and it makes the world exactly 128 * 512 = 65536 units
// across. `observed`
//
// height_scale is weaker: 16 and 32 fit model base elevations about equally
// well (mean error 758 vs 767 world units), because models are not flush with
// the ground. 32 is retained pending better evidence. `inferred`
struct TerrainScale {
    float cell_size = 512.0f;
    float height_scale = 32.0f;
};

// Build a terrain mesh from an ODM heightmap. Produces (dim-1)*(dim-1)*2
// triangles and dim*dim vertices, where dim = OdmTerrain::kGridDim.
[[nodiscard]] TerrainMesh build_terrain_mesh(const world::OdmTerrain& terrain,
                                             TerrainScale scale = {});

// An RGB color (float 0..1) for per-vertex / per-cell coloring.
struct Vec3Color { float r = 0, g = 0, b = 0; };

// A stable color for a tile-type index. Common base tiles get a fixed natural
// palette (grass green, water blue, dirt brown, road tan); all other indices
// get a deterministic hash-to-RGB so each distinct tile is a distinct color.
// See docs/rendering/terrain-coloring.md.
[[nodiscard]] Vec3Color tile_type_color(std::uint8_t tile_index);

// Build a per-vertex color array (dim*dim, row-major) from a tilemap: each
// vertex takes the color of its cell's tile type. Length matches mesh.vertices.
[[nodiscard]] std::vector<Vec3Color>
build_terrain_colors(const world::OdmTerrain& terrain);

}  // namespace starhaven::render

#endif  // STARHAVEN_CORE_RENDER_TERRAIN_MESH_HPP
