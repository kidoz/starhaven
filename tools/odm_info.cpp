#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <span>
#include <string>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/odm_map.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.odm>\n"
              << "\n"
              << "Decompresses one .odm outdoor map from your own legal game\n"
              << "install (Games.lod) and prints its header metadata. The tile\n"
              << "and geometry internals are not yet decoded.\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory, or pass the full Games.lod-relative\n"
              << "map name (e.g. Outa1.odm).\n";
}

std::filesystem::path resolve_games_lod() {
    namespace fs = std::filesystem;
    if (auto install = starhaven::platform::install_from_env()) {
        fs::path p = *install / "data" / "Games.lod";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "data/Games.lod";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string map_name = argv[1];
    // Research mode: one line per model facet, attributes and plane normal.
    const bool dump_facets = argc == 3 && std::string(argv[2]) == "--facets";

    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    lod::GameLodArchive archive;
    const lod::GameLodError open_err = lod::GameLodArchive::open(resolve_games_lod(), archive);
    if (open_err != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod (" << static_cast<int>(open_err) << ")\n";
        return 1;
    }

    std::span<const std::byte> entry;
    const auto pe = archive.payload(map_name, entry);
    if (pe == lod::GameLodArchive::PayloadError::NotFound) {
        std::cerr << "error: map not found in Games.lod: " << map_name << "\n";
        return 1;
    }
    if (pe != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: could not read map payload (" << static_cast<int>(pe) << ")\n";
        return 1;
    }

    world::OdmMap map;
    world::OdmTerrain terrain;
    const world::OdmError parse_err = world::parse_odm_terrain(entry, map, terrain);
    if (parse_err != world::OdmError::None) {
        std::cerr << "error: could not parse ODM (" << static_cast<int>(parse_err) << ")\n";
        return 1;
    }

    const auto& h = map.header;
    std::cout << "map=" << map_name << "\n";
    std::cout << "  name=\"" << h.name << "\"\n";
    std::cout << "  file_name=\"" << h.file_name << "\"\n";
    std::cout << "  version=\"" << h.version << "\"\n";
    std::cout << "  ground=\"" << h.ground_name << "\"\n";
    std::cout << "  tilesets=";
    for (std::size_t i = 0; i < h.tilesets.size(); ++i) {
        if (i) {
            std::cout << ',';
        }
        std::cout << '(' << h.tilesets[i].group << ',' << h.tilesets[i].offset << ')';
    }
    std::cout << "\n";
    std::cout << "  decompressed_payload=" << map.payload.size() << " bytes\n";

    // Terrain stats (non-expressive: only ranges and distinct counts).
    const auto [hmin, hmax] =
        std::minmax_element(terrain.heightmap.begin(), terrain.heightmap.end());
    const auto [tmin, tmax] = std::minmax_element(terrain.tilemap.begin(), terrain.tilemap.end());
    auto distinct = [](const auto& grid) {
        // small grid values; count distinct with a tiny presence table
        std::array<bool, 256> seen{};
        std::size_t n = 0;
        for (auto v : grid) {
            if (!seen[v]) {
                seen[v] = true;
                ++n;
            }
        }
        return n;
    };
    std::cout << "  heightmap: 128x128  range[" << static_cast<int>(*hmin) << ".."
              << static_cast<int>(*hmax) << "]  distinct=" << distinct(terrain.heightmap) << "\n";
    std::cout << "  tilemap:   128x128  range[" << static_cast<int>(*tmin) << ".."
              << static_cast<int>(*tmax) << "]  distinct=" << distinct(terrain.tilemap) << "\n";

    // Placed models (non-expressive: count and a few names).
    std::vector<world::OdmModel> models;
    if (world::extract_models(map, models) == world::OdmError::None) {
        std::cout << "  models: " << models.size() << "\n";
        const std::size_t show = std::min<std::size_t>(models.size(), 5);
        for (std::size_t i = 0; i < show; ++i) {
            std::uint32_t vc = 0;
            world::model_vertex_count(map, i, vc);
            std::cout << "    [" << i << "] " << models[i].name << "  verts=" << vc << "  pos=("
                      << models[i].pos_x << "," << models[i].pos_y << "," << models[i].pos_z
                      << ")\n";
        }
        if (models.size() > show) {
            std::cout << "    ... (" << (models.size() - show) << " more)\n";
        }
    }

    // Model meshes (non-expressive: counts only).
    std::vector<world::OdmModelMesh> meshes;
    if (world::extract_model_meshes(map, meshes) == world::OdmError::None) {
        if (dump_facets) {
            for (const auto& mesh : meshes) {
                for (const auto& f : mesh.facets) {
                    std::cout << f.attributes << "\t" << f.nx() << "\t" << f.ny() << "\t" << f.nz()
                              << "\t" << (f.texture_name.empty() ? "-" : f.texture_name) << "\n";
                }
            }
            return 0;
        }

        std::size_t verts = 0, facets = 0, tris = 0;
        std::set<std::string> textures;
        for (const auto& mesh : meshes) {
            verts += mesh.vertices.size();
            facets += mesh.facets.size();
            for (const auto& f : mesh.facets) {
                // Each n-gon triangulates into n-2 triangles.
                if (f.vertex_count >= 3)
                    tris += f.vertex_count - 2u;
                if (!f.texture_name.empty())
                    textures.insert(f.texture_name);
            }
        }
        std::cout << "  meshes: " << meshes.size() << " models, " << verts << " vertices, "
                  << facets << " facets (" << tris << " triangles), " << textures.size()
                  << " distinct textures\n";
    } else {
        std::cout << "  meshes: geometry stream did not decode\n";
    }

    std::vector<world::OdmDecoration> decorations;
    if (world::extract_decorations(map, decorations) == world::OdmError::None) {
        std::set<std::string> kinds;
        for (const auto& d : decorations)
            kinds.insert(d.name);
        std::cout << "  decorations: " << decorations.size() << " placed, " << kinds.size()
                  << " distinct sprites\n";
    } else {
        std::cout << "  decorations: did not decode\n";
    }
    return 0;
}
