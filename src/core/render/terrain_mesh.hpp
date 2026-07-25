#ifndef OPENMM6_CORE_RENDER_TERRAIN_MESH_HPP
#define OPENMM6_CORE_RENDER_TERRAIN_MESH_HPP

#include <cstdint>
#include <vector>

#include "core/render/math3d.hpp"
#include "core/world/odm_map.hpp"

namespace openmm6::render {

// A terrain mesh built from an ODM heightmap. Vertices are positioned in world
// space as (cell_x * cell_size, height * height_scale, cell_y * cell_size),
// centered on the origin so the camera starts over the map middle.
struct TerrainMesh {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;  // per-vertex, normalized
    std::vector<std::uint32_t> indices;  // triangles: 3 indices each
};

// World-scale defaults. The MM6 grid is 128x128; one horizontal cell maps to
// `cell_size` world units and one height unit to `height_scale` world units.
struct TerrainScale {
    float cell_size = 64.0f;
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

}  // namespace openmm6::render

#endif  // OPENMM6_CORE_RENDER_TERRAIN_MESH_HPP
