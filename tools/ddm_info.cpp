#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_event.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <events.ddm|events.dlv>\n"
              << "\n"
              << "Decompresses one .ddm (outdoor) or .dlv (indoor) event-data\n"
              << "file from your own legal game install (Games.lod) and prints\n"
              << "non-expressive statistics. The internal event-table layout is\n"
              << "not yet decoded.\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory.\n";
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
    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string name = argv[1];

    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(resolve_games_lod(), archive) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (archive.payload(name, entry) != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: event file not found: " << name << "\n";
        return 1;
    }

    world::MapEventFile ev;
    const world::MapEventError e = world::parse_map_event(entry, ev);
    if (e != world::MapEventError::None) {
        std::cerr << "error: could not parse event file ("
                  << static_cast<int>(e) << ")\n";
        return 1;
    }

    // Non-expressive stats only: sizes and how much of the payload is populated.
    std::size_t nonzero = 0;
    for (auto b : ev.payload) {
        if (b != 0) ++nonzero;
    }
    std::cout << "event-file=" << name << "\n";
    std::cout << "  stored=" << entry.size() << " bytes\n";
    std::cout << "  decompressed=" << ev.payload.size() << " bytes\n";
    std::cout << "  nonzero=" << nonzero << " bytes ("
              << (ev.payload.empty() ? 0 : (100.0 * nonzero / ev.payload.size()))
              << "% populated)\n";

    // First event table: populated records (non-expressive: type + name).
    auto records = world::enumerate_event_table(ev);
    std::cout << "  event_records: " << records.size() << "\n";
    const std::size_t show = std::min<std::size_t>(records.size(), 8);
    for (std::size_t i = 0; i < show; ++i) {
        std::cout << "    [" << i << "] type=" << records[i].type
                  << "  name=\"" << records[i].name << "\"\n";
    }
    if (records.size() > show) {
        std::cout << "    ... (" << (records.size() - show) << " more)\n";
    }
    return 0;
}
