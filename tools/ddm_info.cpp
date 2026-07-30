#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

#include "core/data/game_data.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/item_stats.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/map_event.hpp"
#include "core/world/monster_list.hpp"
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
              << "  --variants   research mode: every placed actor's A/B/C letter\n"
              << "               against its map's encounter slots and its own\n"
              << "               variant byte\n"
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

// Research mode: does an encounter slot's "Dif 1-5" pick the A/B/C
// variant? The shipped event files place ~290 actors whose names carry
// their letter; each is matched to its map's slot by the slot's picture
// base, and the letters are tallied against that slot's difficulty.
int do_variants() {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(resolve_games_lod(), archive) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    data::MapStatsTable maps;
    if (data::load_map_stats(resolve_data_dir(), maps) != data::GameDataError::None) {
        std::cerr << "error: could not read MapStats.txt\n";
        return 1;
    }
    const auto lower = [](std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    };
    lod::LodArchive icons;
    world::MonsterList monsters;
    std::span<const std::byte> list_raw;
    if (lod::LodArchive::open(resolve_data_dir() / "icons.lod", icons) != lod::LodError::None ||
        icons.payload("DMONLIST.BIN", list_raw) != lod::LodArchive::PayloadError::None ||
        world::MonsterList::parse(list_raw, monsters) != world::MonsterListError::None) {
        std::cerr << "error: could not read DMONLIST.BIN\n";
        return 1;
    }
    // tally[dif][letter], letter 0..2 for A..C; and the record's own
    // variant byte cross-tabbed against the letter.
    std::array<std::array<int, 3>, 6> tally{};
    std::array<std::array<int, 16>, 3> variant_byte{};
    std::array<int, 3> unmatched{};
    int placed = 0;
    for (const auto& map : maps.entries()) {
        const std::string stem = map.file_name.substr(0, map.file_name.find('.'));
        std::span<const std::byte> entry;
        std::string file;
        for (const char* extension : {".ddm", ".dlv"}) {
            if (archive.payload(stem + extension, entry) ==
                lod::GameLodArchive::PayloadError::None) {
                file = stem + extension;
                break;
            }
        }
        world::MapEventFile ev;
        if (file.empty() || world::parse_map_event(entry, ev) != world::MapEventError::None) {
            continue;
        }
        for (const auto& actor : world::extract_actors(ev)) {
            // The record names the display name; the letter lives on the
            // DMONLIST row its monster id points at.
            const auto* row =
                actor.monster_id > 0 ? monsters.at(actor.monster_id - 1u) : nullptr;
            if (row == nullptr || row->name.empty()) {
                continue;
            }
            const char letter = row->name.back();
            if (letter != 'A' && letter != 'B' && letter != 'C') {
                continue;
            }
            ++placed;
            ++variant_byte[static_cast<std::size_t>(letter - 'A')]
                          [actor.variant < 16 ? actor.variant : 15];
            const std::string base = lower(row->name.substr(0, row->name.size() - 1));
            int dif = -1;
            for (const auto& slot : map.monsters) {
                if (!slot.empty() && lower(slot.picture) == base) {
                    dif = slot.difficulty;
                }
            }
            if (dif >= 1 && dif <= 5) {
                ++tally[static_cast<std::size_t>(dif)][static_cast<std::size_t>(letter - 'A')];
            } else {
                ++unmatched[static_cast<std::size_t>(letter - 'A')];
            }
        }
    }
    std::cout << placed << " placed actors carry a variant letter\n";
    std::cout << "matched to their map's slot, by the slot's Dif 1-5:\n";
    for (int dif = 1; dif <= 5; ++dif) {
        const auto& row = tally[static_cast<std::size_t>(dif)];
        std::cout << "  Dif " << dif << ":  A=" << row[0] << "  B=" << row[1] << "  C=" << row[2]
                  << "\n";
    }
    std::cout << "  no matching slot:  A=" << unmatched[0] << "  B=" << unmatched[1]
              << "  C=" << unmatched[2] << "\n";
    std::cout << "the record's variant byte, against the id's own letter:\n";
    for (int letter = 0; letter < 3; ++letter) {
        std::cout << "  " << static_cast<char>('A' + letter) << ":";
        for (int v = 0; v < 16; ++v) {
            if (variant_byte[static_cast<std::size_t>(letter)][static_cast<std::size_t>(v)] > 0) {
                std::cout << "  byte " << v << " x"
                          << variant_byte[static_cast<std::size_t>(letter)]
                                         [static_cast<std::size_t>(v)];
            }
        }
        std::cout << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string name = argv[1];
    if (name == "--variants") {
        return do_variants();
    }
    const bool show_doors = argc == 3 && std::string(argv[2]) == "--doors";

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

    world::EventLayout layout;
    if (world::parse_event_layout(ev, layout) != world::EventLayoutError::None) {
        std::cout << "  layout: neither the outdoor nor the indoor section chain fits\n";
        return 0;
    }

    std::cout << "  layout: " << (layout.kind == world::MapEventKind::Indoor ? "indoor" : "outdoor")
              << "  sections at " << layout.actors_offset << ", tail " << layout.tail_size
              << " bytes";
    if (layout.kind == world::MapEventKind::Indoor) {
        std::cout << " (" << layout.state_size << " of fixed state, "
                  << (layout.tail_size - layout.state_size) << " of the rest)";
    }
    std::cout << "\n";

    // Research mode: the door block, decoded and verified against the paired
    // level — every id in range, and every base coordinate the position the
    // level ships that vertex at.
    if (show_doors && layout.kind == world::MapEventKind::Indoor) {
        std::string stem = name;
        if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        std::span<const std::byte> level;
        world::BlvMap blv;
        const bool have_level =
            archive.payload(stem + ".blv", level) == lod::GameLodArchive::PayloadError::None &&
            world::parse_blv(level, blv) == world::BlvError::None;

        std::size_t vertex_ids_ok = 0, vertex_ids_all = 0;
        std::size_t face_ids_ok = 0, face_ids_all = 0;
        std::size_t base_ok = 0, base_all = 0;
        std::size_t array_bytes = 0;
        const auto doors = world::extract_doors(ev);
        for (const auto& door : doors) {
            std::cout << "  door " << door.id << ": attr 0x" << std::hex << door.attributes
                      << std::dec << " dir " << door.dx << "," << door.dy << ","
                      << door.dz << " distance " << door.distance << " speeds "
                      << door.open_speed << "/" << door.close_speed << " vertices "
                      << door.vertex_ids.size() << " faces " << door.face_ids.size() << "\n";
            array_bytes += 2 * (4 * door.vertex_ids.size() + 3 * door.face_ids.size() +
                                door.sector_ids.size());
            if (!have_level) {
                continue;
            }
            for (std::size_t i = 0; i < door.vertex_ids.size(); ++i) {
                ++vertex_ids_all;
                const std::uint16_t vid = door.vertex_ids[i];
                if (vid >= blv.vertices.size()) {
                    continue;
                }
                ++vertex_ids_ok;
                ++base_all;
                const auto& v = blv.vertices[vid];
                if (v.x == door.x_base[i] && v.y == door.y_base[i] && v.z == door.z_base[i]) {
                    ++base_ok;
                }
            }
            for (const std::uint16_t fid : door.face_ids) {
                ++face_ids_all;
                face_ids_ok += fid < blv.faces.size() ? 1 : 0;
            }
        }
        std::cout << "  " << doors.size() << " doors, arrays " << array_bytes << " bytes; "
                  << vertex_ids_ok << "/" << vertex_ids_all << " vertex ids in range, "
                  << face_ids_ok << "/" << face_ids_all << " face ids in range, " << base_ok
                  << "/" << base_all << " bases equal the shipped vertex\n";
    }

    // The paired level declares how much saved state this file carries. It is
    // the only size in the event file that is not self-describing.
    if (layout.kind == world::MapEventKind::Indoor) {
        std::string stem = name;
        if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        std::span<const std::byte> level;
        world::BlvMap map;
        if (archive.payload(stem + ".blv", level) == lod::GameLodArchive::PayloadError::None &&
            world::parse_blv(level, map) == world::BlvError::None) {
            const std::size_t declared = map.header.event_state_bytes;
            const std::size_t actual = layout.tail_size - layout.state_size - 256;
            std::cout << "  state bytes: " << actual << " here, " << declared << " declared by "
                      << stem << ".blv" << (actual == declared ? "  (agree)" : "  (DISAGREE)")
                      << "\n";
        }
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
    // The chest record's 140-entry i16 grid: zero on every cell of every
    // shipped chest — runtime loot layout shipped empty, like the outdoor
    // third grid. The count printed keeps the claim checkable.
    {
        std::size_t grid_nonzero = 0;
        const auto* p = reinterpret_cast<const unsigned char*>(ev.payload.data());
        for (std::size_t c = 0; c < layout.chest_count; ++c) {
            const std::size_t base = layout.chests_offset + c * world::kChestRecordSize;
            for (std::size_t k = 0; k < world::kChestItemCount; ++k) {
                const std::size_t at = base + world::kChestGridOffset + k * 2;
                grid_nonzero += (p[at] | p[at + 1]) != 0 ? 1 : 0;
            }
        }
        std::cout << "  chest_grid_nonzero: " << grid_nonzero << "\n";
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
