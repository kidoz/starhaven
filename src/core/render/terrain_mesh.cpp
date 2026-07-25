#include "core/render/terrain_mesh.hpp"

#include <cmath>

namespace starhaven::render {

Vec3Color tile_type_color(std::uint8_t tile_index) {
    // Fixed palette for the common base tiles so dominant ground reads natural.
    // Tile ranges observed on real maps (see docs/rendering/terrain-coloring.md):
    //   ~90       base grass
    //   1..3      paths / bare dirt
    //   160s      textured ground band
    //   180s-200s water / transitions
    switch (tile_index) {
    case 90:
    case 91:
    case 92:  // grass base family
        return {0.30f, 0.55f, 0.22f};
    case 1:
    case 2:
    case 3:
    case 11:  // paths / dirt
        return {0.55f, 0.42f, 0.24f};
    default:
        if (tile_index >= 180) {  // water / transitions
            return {0.15f, 0.35f, 0.65f};
        }
        if (tile_index >= 160 && tile_index < 180) {  // textured ground
            return {0.45f, 0.50f, 0.30f};
        }
        break;
    }
    // Deterministic hash-to-RGB for any other tile index: spread hues evenly.
    const float h = static_cast<float>(tile_index) * 0.61803398875f;  // golden ratio
    const float c = h - std::floor(h);
    // Simple HSV->RGB-ish with fixed S/V for stable, distinguishable colors.
    const float s = 0.5f, v = 0.6f;
    const float f = c * 6.0f;
    const int i = static_cast<int>(std::floor(f));
    const float f1 = f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f1);
    const float t = v * (1.0f - s * (1.0f - f1));
    switch (i % 6) {
    case 0:
        return {v, t, p};
    case 1:
        return {q, v, p};
    case 2:
        return {p, v, t};
    case 3:
        return {p, q, v};
    case 4:
        return {t, p, v};
    default:
        return {v, p, q};
    }
}

std::vector<Vec3Color> build_terrain_colors(const world::OdmTerrain& terrain) {
    constexpr int dim = world::OdmTerrain::kGridDim;  // 128
    std::vector<Vec3Color> colors;
    colors.reserve(static_cast<std::size_t>(dim) * dim);
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * dim + x;
            colors.push_back(tile_type_color(terrain.tilemap[idx]));
        }
    }
    return colors;
}

TerrainMesh build_terrain_mesh(const world::OdmTerrain& terrain, TerrainScale scale) {
    using world::OdmTerrain;
    constexpr int dim = OdmTerrain::kGridDim;  // 128

    TerrainMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(dim) * dim);
    mesh.normals.reserve(static_cast<std::size_t>(dim) * dim);
    mesh.uvs.reserve(static_cast<std::size_t>(dim) * dim);
    mesh.indices.reserve(static_cast<std::size_t>(dim - 1) * (dim - 1) * 6);
    mesh.tile_ids.reserve(static_cast<std::size_t>(dim - 1) * (dim - 1) * 2);

    const float half = (dim - 1) * scale.cell_size * 0.5f;

    auto height_at = [&](int x, int y) {
        const std::size_t idx = static_cast<std::size_t>(y) * dim + x;
        return static_cast<float>(terrain.heightmap[idx]) * scale.height_scale;
    };

    auto world_pos = [&](int x, int y) {
        return Vec3{x * scale.cell_size - half, height_at(x, y), y * scale.cell_size - half};
    };

    // Vertices, and texture coordinates in cell units (see TerrainMesh::uvs).
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            mesh.vertices.push_back(world_pos(x, y));
            mesh.uvs.push_back(Vec2{static_cast<float>(x), static_cast<float>(y)});
        }
    }

    // Per-vertex normals via finite differences of the height field.
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            const int xm = (x > 0) ? x - 1 : x;
            const int xp = (x < dim - 1) ? x + 1 : x;
            const int ym = (y > 0) ? y - 1 : y;
            const int yp = (y < dim - 1) ? y + 1 : y;
            // dH/dx, dH/dy in height units, scaled to world units.
            const float dhdx =
                (height_at(xp, y) - height_at(xm, y)) / (static_cast<float>(xp - xm));
            const float dhdz =
                (height_at(x, yp) - height_at(x, ym)) / (static_cast<float>(yp - ym));
            // Surface tangent in X has slope dhdx/height_scale per cell_size.
            // Normal = normalize(-dhdx_world, cell_size, -dhdz_world) where the
            // world slopes are dhdx*height_scale/cell_size.
            const float sx = (dhdx * scale.height_scale) / scale.cell_size;
            const float sz = (dhdz * scale.height_scale) / scale.cell_size;
            mesh.normals.push_back(normalize(Vec3{-sx, 1.0f, -sz}));
        }
    }

    // Two triangles per cell.
    auto idx = [](int x, int y) { return static_cast<std::uint32_t>(y * dim + x); };
    for (int y = 0; y < dim - 1; ++y) {
        for (int x = 0; x < dim - 1; ++x) {
            const std::uint32_t a = idx(x, y);
            const std::uint32_t b = idx(x + 1, y);
            const std::uint32_t c = idx(x + 1, y + 1);
            const std::uint32_t d = idx(x, y + 1);
            // Winding is CW when viewed from above (+Y) in world space, which
            // is deliberate: projection flips Y when converting NDC to screen
            // rows, and that flip reverses orientation. A world-space CW top
            // face therefore arrives at the rasterizer as screen-space CCW
            // (positive area), which is what draw_triangle treats as
            // front-facing. Emitting world-space CCW here instead would make
            // backface culling discard the entire terrain.
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);

            // Both triangles of the cell take the cell's own tile index, read
            // at its top-left corner — the same convention build_terrain_colors
            // uses, so texture and color paths agree on which tile a cell is.
            const std::uint8_t tile = terrain.tilemap[static_cast<std::size_t>(y) * dim + x];
            mesh.tile_ids.push_back(tile);
            mesh.tile_ids.push_back(tile);
        }
    }

    return mesh;
}

}  // namespace starhaven::render
