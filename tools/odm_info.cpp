#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <span>
#include <string>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/odm_map.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.odm> [--facets|--events|--index|--tail [FILE]]\n"
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
    if (argc < 2 || argc > 4) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string map_name = argv[1];
    // Research mode: one line per model facet, attributes and plane normal.
    const bool dump_facets = argc == 3 && std::string(argv[2]) == "--facets";
    const bool dump_events = argc == 3 && std::string(argv[2]) == "--events";
    const bool dump_tail = argc >= 3 && std::string(argv[2]) == "--tail";
    const bool check_index = argc >= 3 && std::string(argv[2]) == "--index";
    const std::string tail_path = (dump_tail && argc >= 4) ? argv[3] : std::string();

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

        // Every facet with an event on it, with its centre in the map's own
        // axes: where to stand to try the thing the event runs.
        if (dump_events) {
            for (const auto& mesh : meshes) {
                for (const auto& f : mesh.facets) {
                    if (f.event_id == 0 || f.vertex_count < 3) {
                        continue;
                    }
                    long cx = 0, cy = 0, cz = 0;
                    bool bad = false;
                    for (std::size_t k = 0; k < f.vertex_count; ++k) {
                        if (f.vertex_ids[k] >= mesh.vertices.size()) {
                            bad = true;
                            break;
                        }
                        const auto& p = mesh.vertices[f.vertex_ids[k]];
                        cx += p.x;
                        cy += p.y;
                        cz += p.z;
                    }
                    if (bad) {
                        continue;
                    }
                    const auto n = static_cast<long>(f.vertex_count);
                    std::cout << "event " << f.event_id << "\tat " << cx / n << "," << cy / n
                              << "," << cz / n << "\t"
                              << (f.texture_name.empty() ? "-" : f.texture_name) << "\n";
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

    // The region after the decorations: see docs/formats/odm-spans.md.
    if (dump_tail) {
        std::uint64_t geometry_end = 0;
        if (world::model_geometry_end(map, geometry_end) != world::OdmError::None) {
            std::cerr << "error: geometry stream did not decode\n";
            return 1;
        }
        std::vector<world::OdmDecoration> decs;
        if (world::extract_decorations(map, decs) != world::OdmError::None) {
            std::cerr << "error: decorations did not decode\n";
            return 1;
        }
        const std::uint64_t start = geometry_end + 4 +
                                    static_cast<std::uint64_t>(decs.size()) *
                                        (world::kDecorationRecordSize + world::kDecorationNameSize);
        std::cout << "  tail: [" << start << ".." << map.payload.size()
                  << ") = " << (map.payload.size() - start) << " bytes\n";
        if (!tail_path.empty()) {
            std::ofstream out(tail_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(map.payload.data() + start),
                      static_cast<std::streamsize>(map.payload.size() - start));
            std::cout << "  written to " << tail_path << "\n";
        }
        return 0;
    }

    // Research mode: check the index against the rule it is built by.
    if (check_index) {
        world::OdmTileIndex index;
        std::vector<world::OdmDecoration> decs;
        if (world::extract_tile_index(map, index) != world::OdmError::None ||
            world::extract_decorations(map, decs) != world::OdmError::None) {
            std::cerr << "error: the tail did not decode\n";
            return 1;
        }
        std::size_t exact = 0;
        for (std::size_t d = 0; d < decs.size(); ++d) {
            std::set<int> listed;
            std::set<int> predicted;
            for (int ty = 0; ty < world::OdmTileIndex::kDim; ++ty) {
                const float cy = static_cast<float>((64 - ty) * 512 + 256);
                for (int tx = 0; tx < world::OdmTileIndex::kDim; ++tx) {
                    const float cx = static_cast<float>((tx - 64) * 512 + 256);
                    const float dx = cx - static_cast<float>(decs[d].x);
                    const float dy = cy - static_cast<float>(decs[d].y);
                    if (dx * dx + dy * dy <= world::kTileIndexRadius * world::kTileIndexRadius) {
                        predicted.insert(ty * world::OdmTileIndex::kDim + tx);
                    }
                    for (const std::uint16_t pid : index.at(tx, ty)) {
                        if (world::pid_type(pid) == world::kPidDecoration &&
                            world::pid_id(pid) == d) {
                            listed.insert(ty * world::OdmTileIndex::kDim + tx);
                        }
                    }
                }
            }
            exact += listed == predicted ? 1 : 0;
        }
        std::cout << "  " << exact << "/" << decs.size()
                  << " decorations listed exactly where their distance to the tile centre is "
                  << "within " << static_cast<int>(world::kTileIndexRadius) << "\n";
        return 0;
    }

    world::OdmTileIndex index;
    if (world::extract_tile_index(map, index) == world::OdmError::None) {
        std::size_t listed = 0;
        std::size_t terminators = 0;
        std::size_t occupied = 0;
        std::set<int> ids;
        for (int ty = 0; ty < world::OdmTileIndex::kDim; ++ty) {
            for (int tx = 0; tx < world::OdmTileIndex::kDim; ++tx) {
                const auto run = index.at(tx, ty);
                listed += run.size();
                occupied += run.empty() ? 0 : 1;
                for (const std::uint16_t pid : run) {
                    ids.insert(world::pid_id(pid));
                }
            }
        }
        for (const std::uint16_t e : index.entries) {
            terminators += e == 0 ? 1 : 0;
        }
        std::cout << "  tile index: " << index.entries.size() << " entries, " << listed
                  << " references to " << ids.size() << " decorations, " << occupied
                  << " tiles occupied, " << terminators << " terminators\n";
    }

    std::vector<world::OdmSpawnPoint> spawns;
    if (world::extract_spawn_points(map, spawns) == world::OdmError::None) {
        std::set<int> kinds;
        std::set<int> indices;
        for (const auto& s : spawns) {
            kinds.insert(s.kind);
            indices.insert(static_cast<int>(s.index));
        }
        std::cout << "  spawn points: " << spawns.size() << ", kinds";
        for (const int k : kinds) {
            std::cout << " " << k;
        }
        std::cout << ", indices";
        for (const int i : indices) {
            std::cout << " " << i;
        }
        std::cout << "\n";
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
