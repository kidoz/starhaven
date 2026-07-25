// Evidence gathering for the tile-index -> ground-bitmap question.
//
// Reads the user's own legal install read-only and reports non-expressive
// observations: archive inventory, entry names, sizes, and tilemap index
// histograms. It never prints pixel data, decoded art, or any other expressive
// content, and it never writes to the install.
//
// See docs/rendering/terrain-coloring.md for the open question this feeds.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/odm_map.hpp"

namespace fs = std::filesystem;
namespace lod = starhaven::lod;
namespace world = starhaven::world;

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [<map.odm>] [--grep SUBSTR] [--list ARCHIVE]\n"
              << "\n"
              << "Surveys your own legal MM6 install for the ground-tile lookup\n"
              << "chain. Read-only; prints names, sizes and counts only.\n"
              << "\n"
              << "  <map.odm>          also report that map's tileset refs and\n"
              << "                     tilemap index histogram (default Outa1.odm)\n"
              << "  --grep SUBSTR      list entries whose name contains SUBSTR\n"
              << "                     (case-insensitive) across every archive\n"
              << "  --list ARCHIVE     list every entry name in one archive\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install dir.\n";
}

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Non-cryptographic; only for telling "same file as last run" apart. Use
// `shasum -a 256` if a real provenance hash is needed for a research record.
std::uint64_t fnv1a(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return 0;
    std::uint64_t h = 1469598103934665603ULL;
    std::array<char, 65536> buf{};
    while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[static_cast<std::size_t>(i)]);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

fs::path data_dir() {
    if (auto install = starhaven::platform::install_from_env()) {
        if (fs::exists(*install / "data"))
            return *install / "data";
        return *install;
    }
    return "data";
}

// Every candidate container in a stock install, standard-LOD first.
constexpr std::array<const char*, 6> kArchives{"BITMAPS.LOD", "SPRITES.LOD", "icons.lod",
                                               "EVENTS.LOD",  "Games.lod",   "ANIMS.LOD"};

struct Opened {
    std::string name;
    bool is_game_lod = false;
    lod::LodArchive std_archive;
    lod::GameLodArchive game_archive;
};

bool open_archive(const fs::path& path, Opened& out) {
    out.name = path.filename().string();
    // Games.lod uses the container variant; the rest are standard LODs.
    if (lower(out.name).find("games") != std::string::npos) {
        if (lod::GameLodArchive::open(path, out.game_archive) != lod::GameLodError::None) {
            return false;
        }
        out.is_game_lod = true;
        return true;
    }
    return lod::LodArchive::open(path, out.std_archive) == lod::LodError::None;
}

void for_each_entry(const Opened& a,
                    const std::function<void(const std::string&, std::uint32_t)>& fn) {
    if (a.is_game_lod) {
        for (const auto& e : a.game_archive.entries())
            fn(e.name, e.data_size);
    } else {
        for (const auto& e : a.std_archive.entries())
            fn(e.name, e.stored_size);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_name = "Outa1.odm";
    std::string grep_term;
    std::string list_archive;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--grep" && i + 1 < argc) {
            grep_term = lower(argv[++i]);
        } else if (a == "--list" && i + 1 < argc) {
            list_archive = argv[++i];
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            map_name = a;
        }
    }

    const fs::path dir = data_dir();
    std::cout << "install data dir: " << dir << "\n";
    if (!fs::exists(dir)) {
        std::cerr << "error: data directory not found. Set " << starhaven::platform::kInstallEnvVar
                  << ".\n";
        return 1;
    }

    // --- 1. Archive inventory (provenance) ---------------------------------
    std::cout << "\n== archives ==\n";
    std::vector<std::pair<fs::path, Opened>> opened;
    for (const char* name : kArchives) {
        fs::path p = dir / name;
        if (!fs::exists(p)) {
            // Try a case-insensitive match; installs vary.
            for (const auto& de : fs::directory_iterator(dir)) {
                if (lower(de.path().filename().string()) == lower(name)) {
                    p = de.path();
                    break;
                }
            }
        }
        if (!fs::exists(p)) {
            std::printf("  %-14s  (absent)\n", name);
            continue;
        }
        Opened o;
        const bool ok = open_archive(p, o);
        std::printf(
            "  %-14s  %10ju bytes  fnv1a=%016llx  entries=%zu%s\n", name,
            static_cast<std::uintmax_t>(fs::file_size(p)),
            static_cast<unsigned long long>(fnv1a(p)),
            ok ? (o.is_game_lod ? o.game_archive.entries().size() : o.std_archive.entries().size())
               : 0,
            ok ? "" : "  [could not parse]");
        if (ok)
            opened.emplace_back(p, std::move(o));
    }

    // --- 2. Name search across archives ------------------------------------
    if (!grep_term.empty()) {
        std::cout << "\n== entries matching \"" << grep_term << "\" ==\n";
        for (const auto& [path, a] : opened) {
            int hits = 0;
            for_each_entry(a, [&](const std::string& n, std::uint32_t sz) {
                if (lower(n).find(grep_term) != std::string::npos) {
                    std::printf("  %-14s %-20s %8u bytes\n", a.name.c_str(), n.c_str(), sz);
                    ++hits;
                }
            });
            if (hits == 0) {
                std::printf("  %-14s (no match)\n", a.name.c_str());
            }
        }
    }

    // --- 3. Full listing of one archive ------------------------------------
    if (!list_archive.empty()) {
        std::cout << "\n== all entries in " << list_archive << " ==\n";
        for (const auto& [path, a] : opened) {
            if (lower(a.name) != lower(list_archive))
                continue;
            for_each_entry(a, [&](const std::string& n, std::uint32_t sz) {
                std::printf("  %-24s %8u\n", n.c_str(), sz);
            });
        }
    }

    // --- 4. The map's own tileset references + tilemap histogram -----------
    std::cout << "\n== map: " << map_name << " ==\n";
    lod::GameLodArchive games;
    fs::path games_path = dir / "Games.lod";
    if (!fs::exists(games_path)) {
        for (const auto& de : fs::directory_iterator(dir)) {
            if (lower(de.path().filename().string()) == "games.lod") {
                games_path = de.path();
                break;
            }
        }
    }
    if (lod::GameLodArchive::open(games_path, games) != lod::GameLodError::None) {
        std::cerr << "  error: could not open Games.lod\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (games.payload(map_name, entry) != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "  error: map not found in Games.lod: " << map_name << "\n";
        return 1;
    }
    world::OdmMap map;
    world::OdmTerrain terrain;
    if (world::parse_odm_terrain(entry, map, terrain) != world::OdmError::None) {
        std::cerr << "  error: could not parse ODM\n";
        return 1;
    }

    std::cout << "  ground_name : \"" << map.header.ground_name << "\"\n";
    for (std::size_t i = 0; i < map.header.tilesets.size(); ++i) {
        std::printf("  tileset[%zu]  : group=%d offset=%d\n", i, map.header.tilesets[i].group,
                    map.header.tilesets[i].offset);
    }

    std::map<std::uint8_t, int> hist;
    for (std::uint8_t t : terrain.tilemap)
        ++hist[t];
    std::vector<std::pair<std::uint8_t, int>> sorted(hist.begin(), hist.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("  distinct tile indices: %zu\n", sorted.size());
    std::cout << "  index  cells   (descending)\n";
    for (std::size_t i = 0; i < sorted.size() && i < 24; ++i) {
        std::printf("    %3u  %5d\n", sorted[i].first, sorted[i].second);
    }

    // --- 5. Cross-reference the ground name against BITMAPS.LOD ------------
    std::string stem = lower(map.header.ground_name);
    while (!stem.empty() && stem.back() == '\0')
        stem.pop_back();
    if (stem.size() > 4)
        stem = stem.substr(0, 4);
    if (!stem.empty()) {
        std::cout << "\n== BITMAPS.LOD entries starting with \"" << stem
                  << "\" (ground tileset candidates) ==\n";
        for (const auto& [path, a] : opened) {
            if (lower(a.name).find("bitmaps") == std::string::npos)
                continue;
            int hits = 0;
            for_each_entry(a, [&](const std::string& n, std::uint32_t sz) {
                if (lower(n).rfind(stem, 0) == 0) {
                    std::printf("  %-20s %8u bytes\n", n.c_str(), sz);
                    ++hits;
                }
            });
            std::printf("  (%d matching entries)\n", hits);
        }
    }

    return 0;
}
