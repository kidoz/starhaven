// Reads one map's event script and strings from your own legal install.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_script.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map stem> [event]\n"
              << "\n"
              << "Prints a map's .EVT script and .STR strings from icons.lod.\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    const auto install = starhaven::platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << starhaven::platform::kInstallEnvVar << "\n";
        return 1;
    }
    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return 1;
    }

    const std::string stem = argv[1];
    std::span<const std::byte> raw;
    world::MapScript script;
    if (icons.payload(stem + ".EVT", raw) != lod::LodArchive::PayloadError::None ||
        world::MapScript::parse(raw, script) != world::MapScriptError::None) {
        std::cerr << "error: no script for " << stem << "\n";
        return 1;
    }
    world::MapStrings strings;
    if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
        (void)world::MapStrings::parse(raw, strings);
    }

    if (argc == 3) {
        const auto id = static_cast<std::uint16_t>(std::atoi(argv[2]));
        const auto steps = script.event(id);
        if (steps.empty()) {
            std::cerr << "error: " << stem << " has no event " << id << "\n";
            return 1;
        }
        std::cout << "event " << id << ", " << steps.size() << " steps\n";
        for (const auto& step : steps) {
            std::cout << "  " << static_cast<int>(step.sequence) << ": opcode "
                      << static_cast<int>(step.opcode) << " (" << step.arguments.size()
                      << " bytes)";
            for (const std::uint8_t b : step.arguments) {
                std::cout << " " << std::hex << static_cast<int>(b) << std::dec;
            }
            // The three opcodes whose first argument is a string.
            if (world::names_a_string(step.opcode) && !step.arguments.empty()) {
                std::cout << "  \x22" << strings.at(step.arguments.front()) << "\x22";
            }
            std::cout << "\n";
        }
        return 0;
    }

    std::size_t events = 0;
    std::uint16_t last = 0;
    for (const auto& step : script.steps()) {
        if (step.event_id != last) {
            ++events;
            last = step.event_id;
        }
    }
    std::cout << stem << ": " << script.size() << " steps in " << events << " events, "
              << strings.size() << " strings\n";
    for (std::size_t i = 0; i < strings.size() && i < 12; ++i) {
        if (!strings.at(i).empty()) {
            std::cout << "  [" << i << "] " << strings.at(i) << "\n";
        }
    }
    return 0;
}
