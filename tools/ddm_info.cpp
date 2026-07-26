#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

#include "core/data/game_data.hpp"
#include "core/data/item_stats.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_event.hpp"
#include "core/world/object_table.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <events.ddm|events.dlv>\n"
              << "\n"
              << "Decompresses one .ddm (outdoor) or .dlv (indoor) event-data\n"
              << "file from your own legal game install (Games.lod) and prints\n"
              << "non-expressive statistics. Outdoor files also report their\n"
              << "counted actors, sprite objects, and chests.\n"
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

std::filesystem::path resolve_data_dir() {
    namespace fs = std::filesystem;
    if (auto install = starhaven::platform::install_from_env()) {
        fs::path path = *install / "data";
        if (fs::exists(path / "icons.lod")) {
            return path;
        }
    }
    return "data";
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
        std::cerr << "error: could not parse event file (" << static_cast<int>(e) << ")\n";
        return 1;
    }

    // Non-expressive stats only: sizes and how much of the payload is populated.
    std::size_t nonzero = 0;
    for (auto b : ev.payload) {
        if (b != 0)
            ++nonzero;
    }
    std::cout << "event-file=" << name << "\n";
    std::cout << "  stored=" << entry.size() << " bytes\n";
    std::cout << "  decompressed=" << ev.payload.size() << " bytes\n";
    std::cout << "  nonzero=" << nonzero << " bytes ("
              << (ev.payload.empty() ? 0 : (100.0 * nonzero / ev.payload.size()))
              << "% populated)\n";

    world::OutdoorEventLayout layout;
    if (world::parse_outdoor_event_layout(ev, layout) != world::OutdoorEventLayoutError::None) {
        std::cout << "  outdoor-layout: not present\n";
        return 0;
    }

    std::cout << "  actors: " << layout.actor_count << "\n";
    std::cout << "  sprite_objects: " << layout.sprite_object_count << "\n";
    std::cout << "  chests: " << layout.chest_count << "\n";

    const auto actors = world::extract_actors(ev);
    for (std::size_t i = 0; i < actors.size() && i < 3; ++i) {
        std::cout << "    [" << i << "] " << actors[i].name << " (monster "
                  << static_cast<int>(actors[i].monster_id) << ") at (" << actors[i].x << ","
                  << actors[i].y << "," << actors[i].z << ")\n";
    }

    const auto objects = world::extract_sprite_objects(ev);
    world::ObjectTable descriptors;
    lod::LodArchive icons;
    std::span<const std::byte> descriptor_entry;
    const std::filesystem::path data_dir = resolve_data_dir();
    const bool have_descriptors =
        lod::LodArchive::open(data_dir / "icons.lod", icons) == lod::LodError::None &&
        icons.payload("DOBJLIST.BIN", descriptor_entry) == lod::LodArchive::PayloadError::None &&
        world::ObjectTable::parse(descriptor_entry, descriptors) == world::ObjectTableError::None;

    if (have_descriptors) {
        std::size_t joined = 0;
        for (const auto& object : objects) {
            const auto* descriptor = descriptors.at(object.descriptor_index);
            if (descriptor != nullptr && descriptor->object_id == object.object_id) {
                ++joined;
            }
        }
        std::cout << "  descriptor_joins: " << joined << "/" << objects.size() << "\n";
    }

    starhaven::data::ItemStatsTable items;
    const bool have_items =
        starhaven::data::load_item_stats(data_dir, items) == starhaven::data::GameDataError::None;
    if (have_items) {
        std::size_t joined = 0;
        for (const auto& object : objects) {
            if (object.contained_item.item_id >= 0 &&
                items.at(static_cast<std::size_t>(object.contained_item.item_id)) != nullptr) {
                ++joined;
            }
        }
        std::cout << "  item_joins: " << joined << "/" << objects.size() << "\n";
    }

    for (std::size_t i = 0; i < objects.size() && i < 3; ++i) {
        std::cout << "    object[" << i << "] id=" << objects[i].object_id
                  << " descriptor=" << objects[i].descriptor_index
                  << " item=" << objects[i].contained_item.item_id << " at (" << objects[i].x << ","
                  << objects[i].y << "," << objects[i].z << ")";
        if (have_descriptors) {
            if (const auto* descriptor = descriptors.at(objects[i].descriptor_index)) {
                std::cout << " \"" << descriptor->name
                          << "\" frame=" << descriptor->sprite_frame_index;
            }
        }
        if (have_items && objects[i].contained_item.item_id >= 0) {
            if (const auto* item =
                    items.at(static_cast<std::size_t>(objects[i].contained_item.item_id))) {
                std::cout << " item=\"" << starhaven::data::cp1252_to_utf8(item->name)
                          << "\" picture=" << item->picture;
            }
        }
        const auto& state = objects[i].contained_item;
        if (state.standard_bonus_or_potion_power != 0 || state.standard_bonus_strength != 0 ||
            state.special_bonus_or_gold_amount != 0 || state.charges != 0 || state.flags != 0 ||
            state.equipped_slot != 0) {
            std::cout << " state=(" << state.standard_bonus_or_potion_power << ","
                      << state.standard_bonus_strength << "," << state.special_bonus_or_gold_amount
                      << ",charges=" << state.charges << ",flags=0x" << std::hex << state.flags
                      << std::dec << ",slot=" << static_cast<int>(state.equipped_slot) << ")";
        }
        std::cout << "\n";
    }

    const auto chest_items = world::extract_chest_items(ev);
    std::array<std::size_t, 6> random_by_class{};
    std::size_t fixed_items = 0;
    std::size_t fixed_joins = 0;
    std::size_t invalid_negative = 0;
    for (const auto& chest_item : chest_items) {
        const int treasure_class = chest_item.item.random_treasure_class();
        if (treasure_class != 0) {
            ++random_by_class[static_cast<std::size_t>(treasure_class - 1)];
        } else if (chest_item.item.item_id > 0) {
            ++fixed_items;
            if (have_items &&
                items.at(static_cast<std::size_t>(chest_item.item.item_id)) != nullptr) {
                ++fixed_joins;
            }
        } else {
            ++invalid_negative;
        }
    }
    std::cout << "  chest_items: " << chest_items.size() << " random_by_class=";
    for (std::size_t index = 0; index < random_by_class.size(); ++index) {
        if (index != 0) {
            std::cout << ",";
        }
        std::cout << index + 1 << ":" << random_by_class[index];
    }
    if (have_items) {
        std::cout << " fixed_item_joins=" << fixed_joins << "/" << fixed_items;
    } else {
        std::cout << " fixed_items=" << fixed_items;
    }
    std::cout << " invalid_negative=" << invalid_negative << "\n";

    return 0;
}
