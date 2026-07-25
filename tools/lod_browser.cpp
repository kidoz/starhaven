#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <filesystem>

#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << " <list|info|extract> <archive.lod> [name]\n"
              << "\n"
              << "  list    <archive>          enumerate every entry (name, stored size, compressed?)\n"
              << "  info    <archive> [name]   show archive header, or one entry's details\n"
              << "  extract <archive> <name>   write one entry's raw stored bytes to <name>.out\n"
              << "\n"
              << "The archive is read from your own legal game install. Set "
              << starhaven::platform::kInstallEnvVar << " to the install directory,\n"
              << "or pass the full archive path directly.\n";
}

const char* kind_name(starhaven::lod::LodKind k) {
    switch (k) {
        case starhaven::lod::LodKind::Bitmaps: return "bitmaps";
        case starhaven::lod::LodKind::Sprites:  return "sprites";
        case starhaven::lod::LodKind::Icons:    return "icons";
        case starhaven::lod::LodKind::Game:     return "game";
        default:                              return "unknown";
    }
}

// Sniff whether a file is a Games.lod container by reading its 80-byte version
// string at offset 4 and checking for the "Game" prefix. Games.lod uses a
// distinct layout (see docs/formats/games-lod.md) handled by GameLodArchive.
bool is_games_lod(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    char ver[5] = {};
    if (!f.seekg(4) || !f.read(ver, 4)) {
        return false;
    }
    return std::string(ver, 4) == "Game";
}

// --- Games.lod commands -----------------------------------------------------

int do_list_game(const std::filesystem::path& resolved) {
    namespace lod = starhaven::lod;
    lod::GameLodArchive a;
    const lod::GameLodError e = lod::GameLodArchive::open(resolved, a);
    if (e != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    std::cout << "kind=games version=" << a.version()
              << " root=" << a.root_name()
              << " entries=" << a.entries().size() << "\n";
    for (const auto& entry : a.entries()) {
        std::cout << "  " << entry.name << "  size=" << entry.data_size << "\n";
    }
    return 0;
}

int do_info_game(const std::filesystem::path& resolved, const std::string* name) {
    namespace lod = starhaven::lod;
    lod::GameLodArchive a;
    const lod::GameLodError e = lod::GameLodArchive::open(resolved, a);
    if (e != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    if (name == nullptr) {
        std::cout << "kind=games version=" << a.version()
                  << " description=\"" << a.description() << "\""
                  << " root=" << a.root_name()
                  << " root_data_offset=" << a.root_data_offset()
                  << " count=" << a.num_items() << "\n";
        return 0;
    }
    auto entry = a.find(*name);
    if (!entry) {
        std::cerr << "error: entry not found: " << *name << "\n";
        return 1;
    }
    std::cout << "name=" << entry->name
              << " data_offset=" << entry->data_offset
              << " size=" << entry->data_size << "\n";
    return 0;
}

int do_extract_game(const std::filesystem::path& resolved, const std::string& name) {
    namespace lod = starhaven::lod;
    lod::GameLodArchive a;
    const lod::GameLodError e = lod::GameLodArchive::open(resolved, a);
    if (e != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    std::span<const std::byte> bytes;
    const auto pe = a.payload(name, bytes);
    if (pe == lod::GameLodArchive::PayloadError::NotFound) {
        std::cerr << "error: entry not found: " << name << "\n";
        return 1;
    }
    if (pe != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: could not read payload (" << static_cast<int>(pe) << ")\n";
        return 1;
    }
    const std::filesystem::path out_path = std::filesystem::path(name).concat(".out");
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "error: cannot write " << out_path << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "error: write failed for " << out_path << "\n";
        return 1;
    }
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path << "\n";
    return 0;
}

// Resolve a possibly-relative archive name (e.g. "data/BITMAPS.LOD") against
// the configured install directory.
std::filesystem::path resolve_archive(const std::string& arg) {
    namespace fs = std::filesystem;
    fs::path p(arg);
    if (p.is_absolute() && fs::exists(p)) {
        return p;
    }
    if (auto install = starhaven::platform::install_from_env()) {
        fs::path resolved = *install / arg;
        if (fs::exists(resolved)) {
            return resolved;
        }
        resolved = *install / "data" / arg;
        if (fs::exists(resolved)) {
            return resolved;
        }
    }
    return p;  // let open() report the IO error
}

int do_list(const std::string& archive_arg) {
    const auto resolved = resolve_archive(archive_arg);
    if (is_games_lod(resolved)) {
        return do_list_game(resolved);
    }
    namespace lod = starhaven::lod;
    lod::LodArchive a;
    const lod::LodError e = lod::LodArchive::open(resolved, a);
    if (e != lod::LodError::None) {
        std::cerr << "error: could not open archive (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    const auto& entries = a.entries();
    std::cout << "kind=" << kind_name(a.kind())
              << " version=" << a.version()
              << " lod_type=" << a.lod_type()
              << " entries=" << entries.size() << "\n";
    for (const auto& entry : entries) {
        std::cout << "  " << entry.name
                  << "  stored=" << entry.stored_size
                  << "  unpacked=" << entry.unpacked_size
                  << (entry.uncompressed ? "  [uncompressed]" : "  [compressed]")
                  << "\n";
    }
    return 0;
}

int do_info(const std::string& archive_arg, const std::string* name) {
    const auto resolved = resolve_archive(archive_arg);
    if (is_games_lod(resolved)) {
        return do_info_game(resolved, name);
    }
    namespace lod = starhaven::lod;
    lod::LodArchive a;
    const lod::LodError e = lod::LodArchive::open(resolved, a);
    if (e != lod::LodError::None) {
        std::cerr << "error: could not open archive (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    if (name == nullptr) {
        std::cout << "kind=" << kind_name(a.kind())
                  << " version=" << a.version()
                  << " lod_type=" << a.lod_type()
                  << " archive_start=" << a.archive_start()
                  << " count=" << a.count() << "\n";
        return 0;
    }
    auto entry = a.find(*name);
    if (!entry) {
        std::cerr << "error: entry not found: " << *name << "\n";
        return 1;
    }
    std::cout << "name=" << entry->name
              << " data_offset=" << entry->data_offset
              << " stored_size=" << entry->stored_size
              << " unpacked_size=" << entry->unpacked_size
              << (entry->uncompressed ? " uncompressed" : " compressed") << "\n";
    return 0;
}

int do_extract(const std::string& archive_arg, const std::string& name) {
    const auto resolved = resolve_archive(archive_arg);
    if (is_games_lod(resolved)) {
        return do_extract_game(resolved, name);
    }
    namespace lod = starhaven::lod;
    lod::LodArchive a;
    const lod::LodError e = lod::LodArchive::open(resolved, a);
    if (e != lod::LodError::None) {
        std::cerr << "error: could not open archive (" << static_cast<int>(e) << ")\n";
        return 1;
    }
    std::span<const std::byte> bytes;
    const auto pe = a.payload(name, bytes);
    if (pe == lod::LodArchive::PayloadError::NotFound) {
        std::cerr << "error: entry not found: " << name << "\n";
        return 1;
    }
    if (pe == lod::LodArchive::PayloadError::Compressed) {
        std::cerr << "error: entry is compressed; decompression is not supported yet\n";
        return 2;
    }
    if (pe != lod::LodArchive::PayloadError::None) {
        std::cerr << "error: could not read payload (" << static_cast<int>(pe) << ")\n";
        return 1;
    }

    const std::filesystem::path out_path = std::filesystem::path(name).concat(".out");
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "error: cannot write " << out_path << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "error: write failed for " << out_path << "\n";
        return 1;
    }
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string command = argv[1];
    const std::string archive_arg = argv[2];

    if (command == "list") {
        return do_list(archive_arg);
    }
    if (command == "info") {
        if (argc >= 4) {
            const std::string name = argv[3];
            return do_info(archive_arg, &name);
        }
        return do_info(archive_arg, nullptr);
    }
    if (command == "extract") {
        if (argc < 4) {
            print_usage(argv[0]);
            return 2;
        }
        return do_extract(archive_arg, argv[3]);
    }
    print_usage(argv[0]);
    return 2;
}
