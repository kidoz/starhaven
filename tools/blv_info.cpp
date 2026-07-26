#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/blv_map.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.blv> [--extras|--faces|--uv]\n"
              << "\n"
              << "Decompresses one .blv indoor map from your own legal game\n"
              << "install's Games.lod and prints non-expressive statistics.\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
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
    // Research mode: dump each face-extra record as raw hex, since most of the
    // 36 bytes are still unidentified.
    const bool dump_extras = argc == 3 && std::string(argv[2]) == "--extras";
    const bool dump_faces = argc == 3 && std::string(argv[2]) == "--faces";
    const bool dump_uv = argc == 3 && std::string(argv[2]) == "--uv";

    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(resolve_games_lod(), archive) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (archive.payload(map_name, entry) != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: map not found: " << map_name << "\n";
        return 1;
    }

    world::BlvMap map;
    if (const world::BlvError e = world::parse_blv(entry, map); e != world::BlvError::None) {
        std::cerr << "error: could not parse BLV (code " << static_cast<int>(e) << ")\n";
        return 1;
    }

    if (dump_extras) {
        for (std::size_t i = 0; i < map.face_extras.size(); ++i) {
            const std::size_t base =
                static_cast<std::size_t>(map.face_extras_offset) + i * world::kBlvFaceExtraSize;
            for (std::size_t k = 0; k < world::kBlvFaceExtraSize; ++k) {
                const auto byte = map.payload[base + k];
                std::cout << "0123456789abcdef"[byte >> 4] << "0123456789abcdef"[byte & 0xF];
            }
            std::cout << "\n";
        }
        return 0;
    }

    if (dump_uv) {
        for (std::size_t i = 0; i < map.faces.size(); ++i) {
            const auto& f = map.faces[i];
            if (f.u.empty()) {
                continue;
            }
            const auto [min_u, max_u] = std::minmax_element(f.u.begin(), f.u.end());
            const auto [min_v, max_v] = std::minmax_element(f.v.begin(), f.v.end());
            std::cout << i << "\t" << *min_u << "\t" << *max_u << "\t" << *min_v << "\t" << *max_v
                      << "\n";
        }
        return 0;
    }

    if (dump_faces) {
        for (std::size_t i = 0; i < map.faces.size(); ++i) {
            const auto& f = map.faces[i];
            std::cout << i << "\t" << f.attributes << "\t"
                      << (f.texture_name.empty() ? "-" : f.texture_name) << "\n";
        }
        return 0;
    }

    std::cout << "map=" << map_name << "\n";
    std::cout << "  name: \"" << map.header.name << "\"  name2: \"" << map.header.name2
              << "\"  kind=" << map.header.kind << "\n";
    std::cout << "  stored=" << entry.size() << "  decompressed=" << map.payload.size() << "\n";
    std::cout << "  vertices: " << map.vertices.size() << "\n";

    // Coordinate extents give a feel for the level's size without reproducing
    // any of its content.
    if (!map.vertices.empty()) {
        auto [minx, maxx] =
            std::minmax_element(map.vertices.begin(), map.vertices.end(),
                                [](const auto& a, const auto& b) { return a.x < b.x; });
        auto [miny, maxy] =
            std::minmax_element(map.vertices.begin(), map.vertices.end(),
                                [](const auto& a, const auto& b) { return a.y < b.y; });
        auto [minz, maxz] =
            std::minmax_element(map.vertices.begin(), map.vertices.end(),
                                [](const auto& a, const auto& b) { return a.z < b.z; });
        std::cout << "    extent x[" << minx->x << ".." << maxx->x << "] y[" << miny->y << ".."
                  << maxy->y << "] z[" << minz->z << ".." << maxz->z << "]\n";
    }

    std::size_t triangles = 0;
    std::size_t invisible = 0;
    std::size_t untextured = 0;
    std::set<std::string> textures;
    for (const auto& f : map.faces) {
        if (f.vertex_count >= 3)
            triangles += f.vertex_count - 2u;
        if (f.invisible())
            ++invisible;
        if (f.texture_name.empty())
            ++untextured;
        else
            textures.insert(f.texture_name);
    }
    std::cout << "  faces: " << map.faces.size() << " (" << triangles << " triangles), "
              << invisible << " invisible, " << untextured << " untextured\n";
    std::cout << "  distinct face textures: " << textures.size() << "\n";
    std::cout << "  index block: " << map.header.index_block_bytes << " bytes\n";
    // Attribute bit 0x80000000 says a face has an extra; the two must agree
    // apart from the sentinel record every map starts its array with.
    std::size_t flagged = 0;
    for (const auto& f : map.faces) {
        if (f.has_extra()) {
            ++flagged;
        }
    }
    std::vector<bool> described(map.faces.size(), false);
    for (const auto& e : map.face_extras) {
        described[e.face_index] = true;
    }
    std::size_t agree = 0;
    std::size_t only_extra = 0;
    std::size_t only_flag = 0;
    for (std::size_t i = 0; i < map.faces.size(); ++i) {
        if (map.faces[i].has_extra() == described[i]) {
            ++agree;
        } else if (described[i]) {
            ++only_extra;
        } else {
            ++only_flag;
        }
    }
    // The texture origin should be the negation of the face's lowest texture
    // coordinate; see docs/formats/blv.md.
    std::size_t origins = 0;
    std::size_t origins_match = 0;
    for (const auto& e : map.face_extras) {
        const auto& f = map.faces[e.face_index];
        if (f.u.empty()) {
            continue;
        }
        const auto min_u = *std::min_element(f.u.begin(), f.u.end());
        const auto min_v = *std::min_element(f.v.begin(), f.v.end());
        if (e.texture_origin_u != 0) {
            ++origins;
            if (e.texture_origin_u + min_u == 0) {
                ++origins_match;
            }
        }
        if (e.texture_origin_v != 0) {
            ++origins;
            if (e.texture_origin_v + min_v == 0) {
                ++origins_match;
            }
        }
    }

    std::cout << "  texture origins: " << origins_match << "/" << origins
              << " equal the face's negated minimum u or v\n";
    std::cout << "  face extras: " << map.face_extras.size() << " (" << flagged
              << " faces flagged; " << agree << " agree, " << only_extra
              << " described without the flag, " << only_flag << " flagged without a record)\n";
    const auto decorations = world::find_decorations(map);
    std::cout << "  decorations found by scan: " << decorations.size() << "\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(decorations.size(), 3); ++i) {
        const auto& d = decorations[i];
        std::cout << "    [" << i << "] " << d.name << "  pos=(" << d.x << "," << d.y << "," << d.z
                  << ")  angle=" << d.angle << "\n";
    }

    std::cout << "  decoded " << map.decoded_bytes << " of " << map.payload.size() << " bytes; "
              << (map.payload.size() - map.decoded_bytes)
              << " remain (rooms/BSP/lights/doors are a later slice)\n";
    return 0;
}
