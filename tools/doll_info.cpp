// Measures the paperdoll art: the doll backdrop, the twelve bodies, the
// per-armor overlays, and the equip coordinates the item table carries.
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "core/data/game_data.hpp"
#include "core/data/item_stats.hpp"
#include "core/image/bitmap.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"

namespace {

using namespace starhaven;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << "\n"
              << "\n"
              << "Research mode: lists the paperdoll entries in icons.lod with their\n"
              << "decoded sizes, and how ITEMS.TXT places item art on the doll.\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

std::string lowered(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1) {
        print_usage(argv[0]);
        return 2;
    }
    const auto install = platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << platform::kInstallEnvVar << "\n";
        return 1;
    }
    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return 1;
    }

    // The doll's own pieces: the backdrop, the hands, the bodies, and every
    // entry shaped like an armor overlay set.
    const auto size_of = [&icons](const std::string& name) -> std::string {
        std::span<const std::byte> raw;
        image::Bitmap bitmap;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            image::decode_bitmap(raw, bitmap) != image::BitmapError::None) {
            return "?";
        }
        return std::to_string(bitmap.width) + "x" + std::to_string(bitmap.height);
    };

    for (const char* name : {"BACKDOLL", "LEFTHAND", "backhand", "MAGLOOP"}) {
        std::span<const std::byte> raw;
        if (icons.payload(name, raw) == lod::LodArchive::PayloadError::None) {
            std::cout << name << "\t" << size_of(name) << "\n";
        }
    }

    std::size_t bodies = 0;
    std::map<std::string, std::vector<std::string>> overlays;  // stem -> parts
    for (const auto& entry : icons.entries()) {
        const std::string name = lowered(entry.name);
        if (name.rfind("body", 0) == 0 && name.size() == 7) {
            ++bodies;
            std::cout << entry.name << "\t" << size_of(entry.name) << "\n";
            continue;
        }
        for (const std::string_view part : {"bod", "arm1", "arm2"}) {
            if (name.size() > part.size() &&
                name.compare(name.size() - part.size(), part.size(), part) == 0) {
                overlays[name.substr(0, name.size() - part.size())].push_back(
                    std::string(part) + " " + size_of(entry.name));
            }
        }
    }
    std::cout << bodies << " bodies\n";
    for (const auto& [stem, parts] : overlays) {
        std::cout << stem << ":";
        for (const auto& part : parts) {
            std::cout << "  " << part;
        }
        std::cout << "\n";
    }

    // And how the item table places art on the doll: the Equip X/Y columns,
    // summarized by equip type.
    data::ItemStatsTable items;
    if (data::load_item_stats(*install / "data", items) != data::GameDataError::None) {
        std::cerr << "error: could not read ITEMS.TXT\n";
        return 1;
    }
    struct Placed {
        int count = 0;
        int with_offset = 0;
        int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    };
    std::map<int, Placed> by_type;
    for (const auto& item : items.entries()) {
        if (item.name.empty()) {
            continue;
        }
        Placed& p = by_type[static_cast<int>(item.equip_type)];
        ++p.count;
        if (item.equip_x != 0 || item.equip_y != 0) {
            ++p.with_offset;
            p.min_x = std::min(p.min_x, item.equip_x);
            p.max_x = std::max(p.max_x, item.equip_x);
            p.min_y = std::min(p.min_y, item.equip_y);
            p.max_y = std::max(p.max_y, item.equip_y);
        }
    }
    std::cout << "equip type\titems\twith x/y\tx range\ty range\n";
    for (const auto& [type, p] : by_type) {
        std::cout << type << "\t" << p.count << "\t" << p.with_offset << "\t" << p.min_x << ".."
                  << p.max_x << "\t" << p.min_y << ".." << p.max_y << "\n";
    }

    // Worn art that ships as an a/b pair: the item's own picture ends in `a`
    // and the archive holds a `b` twin — one garment, two doll layers.
    std::size_t pairs = 0;
    for (const auto& item : items.entries()) {
        if (item.name.empty() || item.picture.empty() || item.picture.back() != 'a') {
            continue;
        }
        std::string twin = item.picture;
        twin.back() = 'b';
        std::span<const std::byte> raw;
        if (icons.payload(twin, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        ++pairs;
        std::cout << item.picture << " " << size_of(item.picture) << " + " << twin << " "
                  << size_of(twin) << "\ttype " << static_cast<int>(item.equip_type) << "\n";
    }
    std::cout << pairs << " a/b pairs\n";
    return 0;
}
