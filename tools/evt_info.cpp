// Reads one map's event script and strings from your own legal install.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>

#include "core/data/game_data.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/npc_stats.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_script.hpp"
#include "core/data/building_stats.hpp"
#include "game/script_walk.hpp"
#include "game/shop.hpp"
#include "game/travel.hpp"
#include <set>

#include "core/image/zlib_util.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/object_table.hpp"
#include "core/world/sprite_frame_table.hpp"
#include "core/world/sound_table.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--scan | <map stem> [event]>\n"
              << "\n"
              << "Prints a map's .EVT script and .STR strings from icons.lod.\n"
              << "\n"
              << "  --scan   research mode: every opcode across all scripts, its\n"
              << "           argument sizes, and any map file name in its arguments\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
}

// Two of the 83 scripts ship with a lowercase extension — D08.evt and
// Pyramid.evt — so the extension test must not care about case.
bool is_script(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    const std::string tail = name.substr(name.size() - 4);
    return tail[0] == '.' && std::tolower(static_cast<unsigned char>(tail[1])) == 'e' &&
           std::tolower(static_cast<unsigned char>(tail[2])) == 'v' &&
           std::tolower(static_cast<unsigned char>(tail[3])) == 't';
}

// Research mode: what does each opcode look like across every script, and do
// any arguments carry a map file name? A transition has to name where it
// goes, and the 67 maps are a closed set to test against.
int do_scan(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    struct OpcodeShape {
        std::size_t uses = 0;
        std::size_t min_size = static_cast<std::size_t>(-1);
        std::size_t max_size = 0;
        std::size_t file_names = 0;         // arguments holding ".odm"/".blv"
        std::string example;                // one such name, with its map
    };
    std::map<int, OpcodeShape> shapes;
    std::size_t scripts = 0;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        ++scripts;
        for (const auto& step : script.steps()) {
            OpcodeShape& shape = shapes[step.opcode];
            ++shape.uses;
            shape.min_size = std::min(shape.min_size, step.arguments.size());
            shape.max_size = std::max(shape.max_size, step.arguments.size());

            // Case-insensitively look for ".odm" or ".blv" in the raw bytes.
            std::string lowered;
            lowered.reserve(step.arguments.size());
            for (const std::uint8_t b : step.arguments) {
                lowered += static_cast<char>(std::tolower(b));
            }
            const std::size_t at = std::min(lowered.find(".odm"), lowered.find(".blv"));
            if (at != std::string::npos) {
                ++shape.file_names;
                if (shape.example.empty()) {
                    // The name starts after the previous NUL, or at the front.
                    const std::size_t start = lowered.rfind('\0', at) + 1;
                    shape.example = name.substr(0, name.size() - 4) + " event " +
                                    std::to_string(step.event_id) + ": \"" +
                                    lowered.substr(start, at + 4 - start) + "\"";
                }
            }
        }
    }

    std::cout << scripts << " scripts\n";
    std::cout << "opcode\tuses\targ bytes\tfile names\n";
    for (const auto& [opcode, shape] : shapes) {
        std::cout << opcode << "\t" << shape.uses << "\t" << shape.min_size;
        if (shape.max_size != shape.min_size) {
            std::cout << ".." << shape.max_size;
        }
        std::cout << "\t" << shape.file_names;
        if (!shape.example.empty()) {
            std::cout << "\te.g. " << shape.example;
        }
        std::cout << "\n";
    }
    return 0;
}

// Research mode: every use of opcode 6 across every script, read as four
// little-endian i32s and a NUL-terminated destination, tested against the
// design table's own set of 67 map file names.
int do_transitions(const starhaven::lod::LodArchive& icons,
                   const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::MapStatsTable maps;
    if (data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
        std::cerr << "error: could not read MapStats.txt\n";
        return 1;
    }
    const auto lower = [](std::string_view text) {
        std::string out;
        for (const char c : text) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };
    std::map<std::string, std::string> known;  // lowered file name -> display name
    for (const auto& m : maps.entries()) {
        known[lower(m.file_name)] = m.name;
    }

    std::map<std::size_t, std::size_t> sizes;
    std::map<std::string, std::size_t> middle_patterns;  // bytes 16..25 as hex
    std::size_t uses = 0;
    std::size_t same_map = 0;
    std::size_t named = 0;
    std::size_t known_names = 0;
    std::map<std::string, std::size_t> destinations;
    std::map<std::string, std::size_t> unknown_names;
    std::int32_t min_coord[3] = {0, 0, 0};
    std::int32_t max_coord[3] = {0, 0, 0};
    std::int32_t max_facing = 0;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 6) {
                continue;
            }
            ++uses;
            ++sizes[step.arguments.size()];
            const auto& a = step.arguments;
            if (a.size() >= 26) {
                std::string middle;
                for (std::size_t i = 16; i < 26; ++i) {
                    char hex[4];
                    std::snprintf(hex, sizeof hex, "%02x ", a[i]);
                    middle += hex;
                }
                ++middle_patterns[middle];
            }
            if (a.size() >= 27 && a[26] == '0' && (a.size() == 28 ? a[27] == 0 : true)) {
                ++same_map;
            }
            if (a.size() >= 16) {
                for (int c = 0; c < 3; ++c) {
                    std::int32_t v = 0;
                    for (int i = 3; i >= 0; --i) {
                        v = (v << 8) | a[static_cast<std::size_t>(c * 4 + i)];
                    }
                    min_coord[c] = std::min(min_coord[c], v);
                    max_coord[c] = std::max(max_coord[c], v);
                }
                std::int32_t facing = 0;
                for (int i = 15; i >= 12; --i) {
                    facing = (facing << 8) | a[static_cast<std::size_t>(i)];
                }
                max_facing = std::max(max_facing, facing);
            }
            if (a.size() > 26) {
                std::string text;
                for (std::size_t i = 26; i < a.size() && a[i] != 0; ++i) {
                    text += static_cast<char>(std::tolower(a[i]));
                }
                if (!text.empty() && text != "0") {
                    ++named;
                    if (known.contains(text)) {
                        ++known_names;
                        ++destinations[name.substr(0, name.size() - 4) + " -> " + text];
                    } else {
                        ++unknown_names[name.substr(0, name.size() - 4) + " -> " + text];
                    }
                }
            }
        }
    }

    std::cout << uses << " uses of opcode 6\n";
    std::cout << "argument sizes:";
    for (const auto& [size, count] : sizes) {
        std::cout << "  " << size << "b x" << count;
    }
    std::cout << "\n";
    std::cout << same_map << " say \"0\" instead of a destination\n";
    std::cout << named << " name a destination; " << known_names
              << " are maps the design table lists, " << destinations.size() << " distinct\n";
    // The trailing pair 24..25 read as a little-endian u16, against the
    // sound table: consecutive runs like 665..675 are id-shaped.
    {
        world::SoundTable sounds;
        std::span<const std::byte> raw2;
        if (icons.payload("DSOUNDS.BIN", raw2) == lod::LodArchive::PayloadError::None &&
            world::SoundTable::parse(raw2, sounds) == world::SoundTableError::None) {
            std::map<int, std::size_t> tails;
            for (const auto& [pattern, count] : middle_patterns) {
                // The pattern is hex bytes "xx " * 10; the last two are 24, 25.
                const int b24 = static_cast<int>(std::strtol(pattern.substr(24, 2).c_str(),
                                                             nullptr, 16));
                const int b25 = static_cast<int>(std::strtol(pattern.substr(27, 2).c_str(),
                                                             nullptr, 16));
                tails[b24 | (b25 << 8)] += count;
            }
            std::cout << "bytes 24..25 as u16 vs DSOUNDS:\n";
            for (const auto& [value, count] : tails) {
                if (value == 0) {
                    continue;
                }
                const auto* row = sounds.find(static_cast<std::uint32_t>(value));
                std::cout << "  " << value << " x" << count << " -> "
                          << (row != nullptr ? row->name : "(no sound)") << "\n";
            }
        }
    }

    std::cout << "bytes 16..25 by pattern:\n";
    for (const auto& [pattern, count] : middle_patterns) {
        std::cout << "  " << pattern << " x" << count << "\n";
    }

    // Are bytes 24..25 a sound id? Test the u16 against the global table.
    world::SoundTable sound_table;
    std::span<const std::byte> sound_raw;
    if (icons.payload("DSOUNDS.BIN", sound_raw) == lod::LodArchive::PayloadError::None &&
        world::SoundTable::parse(sound_raw, sound_table) == world::SoundTableError::None) {
        std::map<std::string, std::size_t> named_sounds;
        std::size_t with_value = 0;
        std::size_t resolve = 0;
        for (const auto& [pattern, count] : middle_patterns) {
            unsigned low = 0, high = 0;
            std::sscanf(pattern.c_str() + 24, "%x", &low);
            std::sscanf(pattern.c_str() + 27, "%x", &high);
            const std::uint32_t value = low | (high << 8);
            if (value == 0) {
                continue;
            }
            with_value += count;
            if (const auto* entry = sound_table.find(value); entry != nullptr) {
                resolve += count;
                named_sounds[entry->name] += count;
            }
        }
        std::cout << "bytes 24..25 as a sound id: " << resolve << "/" << with_value
                  << " nonzero values resolve\n";
        for (const auto& [sound, count] : named_sounds) {
            std::cout << "  " << sound << " x" << count << "\n";
        }
    }
    for (const auto& [pair, count] : destinations) {
        std::cout << "  " << pair << " x" << count << "\n";
    }
    std::cout << "coordinates: x " << min_coord[0] << ".." << max_coord[0] << ", y " << min_coord[1]
              << ".." << max_coord[1] << ", z " << min_coord[2] << ".." << max_coord[2]
              << ", facing 0.." << max_facing << "\n";
    for (const auto& [text, count] : unknown_names) {
        std::cout << "  not in MapStats: " << text << " x" << count << "\n";
    }
    return 0;
}

// Research mode: the shape of the would-be variable opcodes. For each, the
// argument sizes, the first byte's vocabulary (a candidate type selector),
// the little-endian u32 after it (a candidate value), and — for opcode 14 —
// whether the trailing byte stays inside its own event's sequence numbers,
// which is what a jump target would do.
int do_variables(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    struct TypeUse {
        std::size_t uses = 0;
        std::uint32_t min_value = 0xFFFFFFFF;
        std::uint32_t max_value = 0;
    };
    struct OpcodeUse {
        std::map<std::size_t, std::size_t> sizes;
        std::map<int, TypeUse> by_type;      // first byte -> value stats
        std::size_t jump_in_range = 0;       // trailing byte <= event's max sequence
        std::size_t jump_total = 0;
        std::size_t max_trailing = 0;
    };
    const std::vector<int> candidates{1, 14, 15, 16, 17, 18, 19, 32, 36, 37};
    std::map<int, OpcodeUse> opcodes;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        // The largest sequence number of each event, for the jump test.
        std::map<std::uint16_t, std::size_t> max_sequence;
        for (const auto& step : script.steps()) {
            auto& top = max_sequence[step.event_id];
            top = std::max(top, static_cast<std::size_t>(step.sequence));
        }
        for (const auto& step : script.steps()) {
            if (std::find(candidates.begin(), candidates.end(), static_cast<int>(step.opcode)) ==
                candidates.end()) {
                continue;
            }
            OpcodeUse& use = opcodes[step.opcode];
            const auto& a = step.arguments;
            ++use.sizes[a.size()];
            if (a.size() >= 5) {
                std::uint32_t value = 0;
                for (int i = 4; i >= 1; --i) {
                    value = (value << 8) | a[static_cast<std::size_t>(i)];
                }
                TypeUse& t = use.by_type[a[0]];
                ++t.uses;
                t.min_value = std::min(t.min_value, value);
                t.max_value = std::max(t.max_value, value);
            }
            if (a.size() == 6) {
                ++use.jump_total;
                use.max_trailing = std::max(use.max_trailing, static_cast<std::size_t>(a[5]));
                if (a[5] <= max_sequence[step.event_id]) {
                    ++use.jump_in_range;
                }
            }
            // The one- and two-byte opcodes: is their byte a step number too?
            if (a.size() == 1 || a.size() == 2) {
                ++use.jump_total;
                use.max_trailing = std::max(use.max_trailing, static_cast<std::size_t>(a[0]));
                if (a[0] <= max_sequence[step.event_id]) {
                    ++use.jump_in_range;
                }
                if (a.size() == 2) {
                    TypeUse& t = use.by_type[a[1]];
                    ++t.uses;
                    t.min_value = std::min(t.min_value, static_cast<std::uint32_t>(a[0]));
                    t.max_value = std::max(t.max_value, static_cast<std::uint32_t>(a[0]));
                }
            }
        }
    }

    for (const auto& [opcode, use] : opcodes) {
        std::cout << "opcode " << opcode << "\n  sizes:";
        for (const auto& [size, count] : use.sizes) {
            std::cout << "  " << size << "b x" << count;
        }
        std::cout << "\n";
        if (use.jump_total > 0) {
            std::cout << "  trailing byte within its event's sequences: " << use.jump_in_range
                      << "/" << use.jump_total << ", max " << use.max_trailing << "\n";
        }
        for (const auto& [type, t] : use.by_type) {
            std::cout << "  type " << type << "\tx" << t.uses << "\tvalue " << t.min_value << ".."
                      << t.max_value << "\n";
        }
    }
    return 0;
}

// Research mode: OUT.EVT, whose records carry no sequence byte — [size]
// [id u16][opcode][args] — unlike every map script's. Print each record, and
// decode the travel steps as spawn points.
int do_out(const starhaven::lod::LodArchive& icons, const std::string& entry_name) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    if (icons.payload(entry_name, raw) != lod::LodArchive::PayloadError::None ||
        raw.size() < 48) {
        std::cerr << "error: no OUT.EVT\n";
        return 1;
    }
    std::uint32_t unpacked = 0;
    std::memcpy(&unpacked, raw.data() + 0x28, sizeof(unpacked));
    std::vector<std::uint8_t> payload;
    const auto body = raw.subspan(48);
    if (unpacked == 0) {
        payload.assign(reinterpret_cast<const std::uint8_t*>(body.data()),
                       reinterpret_cast<const std::uint8_t*>(body.data()) + body.size());
    } else if (!starhaven::image::detail::inflate_all(body, payload) ||
               payload.size() != unpacked) {
        std::cerr << "error: OUT.EVT container did not inflate\n";
        return 1;
    }

    std::size_t at = 0;
    while (at < payload.size()) {
        const std::size_t size = payload[at];
        if (size < 3 || at + 1 + size > payload.size()) {
            std::cout << "  misframed at byte " << at << "\n";
            break;
        }
        const std::uint16_t id =
            static_cast<std::uint16_t>(payload[at + 1] | (payload[at + 2] << 8));
        const std::uint8_t opcode = payload[at + 3];
        const std::size_t arg_count = size - 3;
        std::cout << "event " << id << "\topcode " << static_cast<int>(opcode) << " ("
                  << arg_count << " bytes)";
        if (opcode == world::kOpcodeTravel && arg_count >= 27) {
            world::ScriptStep step;
            step.opcode = world::kOpcodeTravel;
            step.arguments.assign(payload.begin() + static_cast<std::ptrdiff_t>(at + 4),
                                  payload.begin() + static_cast<std::ptrdiff_t>(at + 1 + size));
            if (const auto travel = world::parse_travel(step)) {
                std::cout << "\t-> " << (travel->destination.empty() ? "0" : travel->destination)
                          << " at " << travel->x << "," << travel->y << "," << travel->z
                          << " facing " << travel->facing;
            }
        } else {
            for (std::size_t i = 0; i < arg_count && i < 6; ++i) {
                std::cout << " " << static_cast<int>(payload[at + 4 + i]);
            }
        }
        std::cout << "\n";
        at += 1 + size;
    }
    return 0;
}

// Research mode: opcode 11, whose arguments read as a u32 and a
// NUL-terminated name. The names are texture names, testable against
// BITMAPS.LOD; the u32 reads as the face to re-texture.
int do_sounds(const starhaven::lod::LodArchive& icons,
              const starhaven::lod::LodArchive& bitmaps) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t uses = 0;
    std::size_t named = 0;
    std::size_t name_is_bitmap = 0;
    std::uint32_t max_id = 0;
    std::map<std::string, std::size_t> unresolved;
    std::map<std::string, std::size_t> resolved;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 11) {
                continue;
            }
            ++uses;
            const auto& a = step.arguments;
            if (a.size() < 5) {
                continue;
            }
            std::uint32_t id = 0;
            for (int i = 3; i >= 0; --i) {
                id = (id << 8) | a[static_cast<std::size_t>(i)];
            }
            max_id = std::max(max_id, id);
            std::string name;
            for (std::size_t i = 4; i < a.size() && a[i] != 0; ++i) {
                name += static_cast<char>(a[i]);
            }
            if (name.empty()) {
                continue;
            }
            ++named;
            if (bitmaps.find(name).has_value()) {
                ++name_is_bitmap;
                ++resolved[name];
            } else {
                ++unresolved[entry.name + " face " + std::to_string(id) + " \"" + name + "\""];
            }
        }
    }
    std::cout << uses << " uses of opcode 11; " << named << " carry a name, " << name_is_bitmap
              << " of those are BITMAPS.LOD entries; face ids run to " << max_id << "\n";
    std::cout << resolved.size() << " distinct textures:\n";
    for (const auto& [name, count] : resolved) {
        std::cout << "  " << name << " x" << count << "\n";
    }
    for (const auto& [example, count] : unresolved) {
        std::cout << "  not a bitmap: " << example << " x" << count << "\n";
    }
    return 0;
}

// Research mode: the events with no opcode-4 header. Where do they live,
// what do they open with, and do the face event ids that resolve nowhere
// point at the shared scripts?
int do_unheaded(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::map<std::string, std::pair<std::size_t, std::size_t>> per_script;  // headed, unheaded
    std::map<int, std::size_t> unheaded_opens;
    std::set<std::uint16_t> global_ids;
    std::map<std::string, world::MapScript> scripts;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        std::uint16_t last = 0xFFFF;
        for (const auto& step : script.steps()) {
            if (step.event_id == last) {
                continue;
            }
            last = step.event_id;
            const bool headed = step.opcode == world::kOpcodeHeader;
            auto& [h, u] = per_script[stem];
            (headed ? h : u) += 1;
            if (!headed) {
                ++unheaded_opens[step.opcode];
            }
            if (stem == "GLOBAL") {
                global_ids.insert(step.event_id);
            }
        }
        scripts.emplace(stem, std::move(script));
    }

    std::size_t headed_total = 0;
    std::size_t unheaded_total = 0;
    std::cout << "unheaded events by script (only scripts that have them):\n";
    for (const auto& [stem, counts] : per_script) {
        headed_total += counts.first;
        unheaded_total += counts.second;
        if (counts.second > 0) {
            std::cout << "  " << stem << ": " << counts.second << " unheaded, " << counts.first
                      << " headed\n";
        }
    }
    std::cout << headed_total << " headed, " << unheaded_total << " unheaded\n";
    std::cout << "unheaded events open with:";
    for (const auto& [opcode, count] : unheaded_opens) {
        std::cout << "  op" << opcode << " x" << count;
    }
    std::cout << "\n";

    // The 33 face event ids that resolve in no map script: are they GLOBAL's?
    lod::GameLodArchive games;
    if (lod::GameLodArchive::open(
            *starhaven::platform::install_from_env() / "data" / "Games.lod", games) !=
        lod::GameLodError::None) {
        return 0;
    }
    std::size_t faces_unresolved = 0;
    std::size_t in_global = 0;
    for (const auto& [stem, script] : scripts) {
        std::span<const std::byte> level;
        world::BlvMap blv;
        if (games.payload(stem + ".blv", level) != lod::GameLodArchive::PayloadError::None ||
            world::parse_blv(level, blv) != world::BlvError::None) {
            continue;
        }
        for (const auto& extra : blv.face_extras) {
            if (extra.event_id == 0 || script.defines(extra.event_id)) {
                continue;
            }
            ++faces_unresolved;
            in_global += global_ids.contains(extra.event_id) ? 1 : 0;
        }
    }
    std::cout << faces_unresolved << " face event ids resolve in no map script; " << in_global
              << " of those are GLOBAL.EVT events\n";

    // And the join the letter quest exposed: an NPC row's event ids against
    // the global script's own event ids.
    starhaven::data::NpcTable npcs;
    if (starhaven::data::load_npcs(*starhaven::platform::install_from_env() / "data", npcs) ==
        starhaven::data::GameDataError::None) {
        std::size_t npc_events = 0;
        std::size_t npc_in_global = 0;
        for (const auto& npc : npcs.entries()) {
            for (const int id : npc.events) {
                if (id <= 0) {
                    continue;
                }
                ++npc_events;
                npc_in_global +=
                    global_ids.contains(static_cast<std::uint16_t>(id)) ? 1 : 0;
            }
        }
        std::cout << npc_events << " NPC event ids; " << npc_in_global
                  << " are GLOBAL.EVT events\n";
    }
    return 0;
}

// Research mode: which unnamed opcode plays a sound? For every opcode and
// every u32 offset inside its arguments, the resolve rate against the global
// sound table — with the names, since a rate alone pattern-matches on the
// dense id regions.
int do_soundsweep(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::SoundTable sounds;
    std::span<const std::byte> raw;
    if (icons.payload("DSOUNDS.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SoundTable::parse(raw, sounds) != world::SoundTableError::None) {
        std::cerr << "error: could not read DSOUNDS.BIN\n";
        return 1;
    }

    struct Candidate {
        std::size_t uses = 0;
        std::size_t resolves = 0;
        std::set<std::uint32_t> values;
        std::map<std::string, std::size_t> names;
    };
    const std::set<int> named{1, 2, 4, 5, 6, 7, 11, 14, 15, 16, 17, 18, 29, 30, 35, 36};
    std::map<std::pair<int, int>, Candidate> candidates;  // (opcode, offset)
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (named.contains(step.opcode) || step.arguments.size() < 4) {
                continue;
            }
            for (std::size_t at = 0; at + 4 <= step.arguments.size(); ++at) {
                std::uint32_t value = 0;
                for (int i = 3; i >= 0; --i) {
                    value = (value << 8) | step.arguments[at + static_cast<std::size_t>(i)];
                }
                if (value == 0) {
                    continue;
                }
                Candidate& c = candidates[{step.opcode, static_cast<int>(at)}];
                ++c.uses;
                if (const auto* found = sounds.find(value); found != nullptr) {
                    ++c.resolves;
                    c.values.insert(value);
                    ++c.names[found->name];
                }
            }
        }
    }

    for (const auto& [key, c] : candidates) {
        if (c.uses < 10 || c.resolves * 2 < c.uses || c.values.size() < 3) {
            continue;  // report only near-total resolve rates with variety
        }
        std::cout << "opcode " << key.first << " +[" << key.second << "]: " << c.resolves << "/"
                  << c.uses << " resolve, " << c.values.size() << " distinct;";
        std::size_t shown = 0;
        for (const auto& [name, count] : c.names) {
            if (++shown > 6) {
                break;
            }
            std::cout << " " << name << " x" << count;
        }
        std::cout << "\n";
    }
    return 0;
}

// Research mode: opcode 4, correlated against what its event's body does.
// For every headed event, compare the header's argument with the argument of
// each kind of working step the body contains.
int do_headers(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    struct Match {
        std::size_t events = 0;
        std::size_t agree = 0;
    };
    std::map<int, Match> by_body;   // body opcode -> header arg equality
    std::map<int, std::size_t> header_values;
    // The other candidate: the header as an index into the map's own .STR —
    // the thing's mouseover name. Measured separately for events that open an
    // establishment (whose header is the 2DEvents row) and the rest.
    std::size_t plain_events = 0, plain_named = 0, enter_events = 0, enter_named = 0;
    std::map<std::string, std::size_t> plain_names;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        world::MapStrings strings;
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        std::uint16_t last = 0xFFFF;
        for (std::size_t i = 0; i < script.steps().size(); ++i) {
            const auto& head = script.steps()[i];
            if (head.event_id == last) {
                continue;
            }
            last = head.event_id;
            if (head.opcode != world::kOpcodeHeader || head.arguments.empty()) {
                continue;
            }
            const int header = head.arguments.front();
            ++header_values[header];
            bool has_enter = false;
            for (std::size_t k = i + 1;
                 k < script.steps().size() && script.steps()[k].event_id == head.event_id; ++k) {
                has_enter = has_enter || script.steps()[k].opcode == world::kOpcodeEnter;
            }
            const std::string_view named =
                static_cast<std::size_t>(header) < strings.size() ? strings.at(header) : "";
            if (has_enter) {
                ++enter_events;
                enter_named += named.empty() ? 0 : 1;
            } else {
                ++plain_events;
                if (!named.empty()) {
                    ++plain_named;
                    ++plain_names[std::string(named)];
                }
            }
            // The body's working steps, each kind counted once per event.
            std::set<int> seen;
            for (std::size_t k = i + 1;
                 k < script.steps().size() && script.steps()[k].event_id == head.event_id; ++k) {
                const auto& step = script.steps()[k];
                if (step.arguments.empty() || !seen.insert(step.opcode).second) {
                    continue;
                }
                Match& match = by_body[step.opcode];
                ++match.events;
                match.agree += step.arguments.front() == header ? 1 : 0;
            }
        }
    }
    std::cout << "header arg vs first arg of each body opcode:\n";
    for (const auto& [opcode, match] : by_body) {
        if (match.events < 20) {
            continue;
        }
        std::cout << "  op" << opcode << ": " << match.agree << "/" << match.events << "\n";
    }
    std::cout << "header as .STR index: names " << plain_named << "/" << plain_events
              << " non-establishment events, " << enter_named << "/" << enter_events
              << " establishment events; the names:\n";
    {
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [name, count] : plain_names) {
            ranked.emplace_back(count, name);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::size_t listed = 0;
        for (const auto& [count, name] : ranked) {
            if (++listed > 20) {
                std::cout << "  ...\n";
                break;
            }
            std::cout << "  \"" << name << "\" x" << count << "\n";
        }
    }
    std::cout << "header values:";
    std::size_t shown = 0;
    for (const auto& [value, count] : header_values) {
        if (++shown > 24) {
            std::cout << " ...";
            break;
        }
        std::cout << " " << value << " x" << count;
    }
    std::cout << "\n";
    return 0;
}

// Research mode: opcodes 39 and 40, read as NPC mutations and tested against
// the NPC table's own id and topic-slot spaces.
int do_npc_mutations(const starhaven::lod::LodArchive& icons,
                     const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::NpcTable npcs;
    if (data::load_npcs(data_dir, npcs) != data::GameDataError::None) {
        std::cerr << "error: could not read NPCdata.txt\n";
        return 1;
    }
    data::NpcDialogueTable dialogue;
    (void)data::load_npc_dialogue(data_dir, dialogue);
    const std::size_t npc_count = npcs.entries().size();

    std::span<const std::byte> raw;
    const auto u32_at = [](const std::vector<std::uint8_t>& a, std::size_t at) {
        std::uint32_t value = 0;
        for (int i = 3; i >= 0; --i) {
            value = (value << 8) | a[at + static_cast<std::size_t>(i)];
        }
        return value;
    };

    std::size_t op39_uses = 0, op39_npc_ok = 0, op39_topic_ok = 0;
    std::map<int, std::size_t> op39_slots;
    std::size_t op40_uses = 0, op40_npc_ok = 0, op40_place_ok = 0, op40_zero = 0;
    std::uint32_t op40_max_place = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode == 39 && step.arguments.size() >= 9) {
                ++op39_uses;
                const std::uint32_t npc = u32_at(step.arguments, 0);
                const int slot = step.arguments[4];
                const std::uint32_t topic = u32_at(step.arguments, 5);
                op39_npc_ok += npc >= 1 && npc <= npc_count ? 1 : 0;
                ++op39_slots[slot];
                op39_topic_ok += dialogue.at(static_cast<int>(topic)) != nullptr || topic == 0 ? 1 : 0;
                if (dialogue.at(static_cast<int>(topic)) == nullptr && topic != 0)
                    std::cout << "  unresolved topic " << topic << "\n";
            } else if (step.opcode == 40 && step.arguments.size() >= 8) {
                ++op40_uses;
                const std::uint32_t npc = u32_at(step.arguments, 0);
                const std::uint32_t place = u32_at(step.arguments, 4);
                op40_npc_ok += npc >= 1 && npc <= npc_count ? 1 : 0;
                op40_zero += place == 0 ? 1 : 0;
                op40_place_ok += place >= 1 && place <= 557 ? 1 : 0;
                op40_max_place = std::max(op40_max_place, place);
            }
        }
    }
    std::cout << "op39 [npc u32][slot u8][topic u32]: " << op39_uses << " uses, npc in 1.."
              << npc_count << " on " << op39_npc_ok << ", topic resolves on " << op39_topic_ok
              << "; slots:";
    for (const auto& [slot, count] : op39_slots) {
        std::cout << " " << slot << " x" << count;
    }
    std::cout << "\n";
    std::cout << "op40 [npc u32][place u32]: " << op40_uses << " uses, npc in range on "
              << op40_npc_ok << ", place zero on " << op40_zero << ", place in 1..557 on "
              << op40_place_ok << ", max place " << op40_max_place << "\n";
    return 0;
}

// Research mode: the remaining common opcodes, each use printed compactly
// with its script and event, decoded by its candidate shape.
int do_catalog(const starhaven::lod::LodArchive& icons, int wanted) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t shown = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (static_cast<int>(step.opcode) != wanted || shown >= 2200) {
                continue;
            }
            ++shown;
            std::cout << entry.name.substr(0, entry.name.size() - 4) << " " << step.event_id
                      << ":";
            const auto& a = step.arguments;
            if (wanted == 19 && a.size() >= 15) {
                // Candidate shape: a kind, then three i32 coordinates.
                const auto i32_at = [&a](std::size_t at) {
                    std::uint32_t v = 0;
                    for (int i = 3; i >= 0; --i) {
                        v = (v << 8) | a[at + static_cast<std::size_t>(i)];
                    }
                    return static_cast<std::int32_t>(v);
                };
                std::cout << " (" << static_cast<int>(a[0]) << "," << static_cast<int>(a[1])
                          << "," << static_cast<int>(a[2]) << ") at " << i32_at(3) << ","
                          << i32_at(7) << "," << i32_at(11);
            } else {
                for (const std::uint8_t b : a) {
                    std::cout << " " << static_cast<int>(b);
                }
            }
            std::cout << "\n";
        }
    }
    std::cout << shown << " shown\n";
    return 0;
}

// Research mode: opcode 21's u16 against the object table's own ids — a
// projectile has to be some object.
int do_projectiles(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::ObjectTable objects;
    std::span<const std::byte> raw;
    if (icons.payload("DOBJLIST.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::ObjectTable::parse(raw, objects) != world::ObjectTableError::None) {
        std::cerr << "error: could not read DOBJLIST.BIN\n";
        return 1;
    }
    std::set<std::uint16_t> ids;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (const auto* d = objects.at(i); d != nullptr) {
            ids.insert(d->object_id);
        }
    }

    std::size_t uses = 0, resolve = 0;
    std::map<std::string, std::size_t> names;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 21 || step.arguments.size() < 27) {
                continue;
            }
            ++uses;
            const auto id = static_cast<std::uint16_t>(step.arguments[0] |
                                                       (step.arguments[1] << 8));
            if (!ids.contains(id)) {
                continue;
            }
            ++resolve;
            for (std::size_t i = 0; i < objects.size(); ++i) {
                if (const auto* d = objects.at(i); d != nullptr && d->object_id == id) {
                    ++names[d->name + " (" + std::to_string(id) + ")"];
                    break;
                }
            }
        }
    }
    std::cout << uses << " uses of opcode 21; the u16 is an object id on " << resolve << "\n";
    for (const auto& [name, count] : names) {
        std::cout << "  " << name << " x" << count << "\n";
    }
    return 0;
}

// Research mode: opcode 21's u16 against the sprite frame table. Two candidate
// readings — the Nth group in file order, or a raw frame index — printed side
// by side with the two points, so the names and the maps can judge.
int do_launches(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::SpriteFrameTable table;
    std::span<const std::byte> raw;
    if (icons.payload("DSFT.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SpriteFrameTable::parse(raw, table) != world::SpriteFrameError::None) {
        std::cerr << "error: could not read DSFT.BIN\n";
        return 1;
    }
    // The Nth group's name, in the order the file stores its frames.
    std::vector<std::string> group_names;
    for (const auto& frame : table.frames()) {
        if (!frame.group_name.empty()) {
            group_names.push_back(frame.group_name);
        }
    }

    std::size_t uses = 0, group_hits = 0, frame_start_hits = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            const auto& a = step.arguments;
            if (step.opcode != 21 || a.size() < 27) {
                continue;
            }
            ++uses;
            const auto id = static_cast<std::uint16_t>(a[0] | (a[1] << 8));
            const auto i32_at = [&a](std::size_t at) {
                std::uint32_t v = 0;
                for (int i = 3; i >= 0; --i) {
                    v = (v << 8) | a[at + static_cast<std::size_t>(i)];
                }
                return static_cast<std::int32_t>(v);
            };
            const bool as_group = id < group_names.size();
            const bool as_frame_start =
                id < table.size() && table.frames()[id].starts_group();
            group_hits += as_group ? 1 : 0;
            frame_start_hits += as_frame_start ? 1 : 0;
            std::cout << entry.name.substr(0, entry.name.size() - 4) << " " << step.event_id
                      << ": group " << id << " = "
                      << (as_group ? group_names[id] : std::string("<out of range>"))
                      << (as_frame_start
                              ? " (as frame: " + table.frames()[id].group_name + ")"
                              : " (as frame: not a group start)")
                      << " speed? " << static_cast<int>(a[2]) << " from " << i32_at(3) << ","
                      << i32_at(7) << "," << i32_at(11) << " to " << i32_at(15) << ","
                      << i32_at(19) << "," << i32_at(23) << "\n";
        }
    }
    std::cout << uses << " uses; u16 in group range on " << group_hits
              << ", starts a group as a frame index on " << frame_start_hits << "\n";
    return 0;
}

// Research mode: the three shelf opcodes, each against its candidate reading.
// 26 as a typed-answer ask: three string indices and a success step. 25 as a
// random jump: up to six step numbers, zero-padded. 32 as an event switch:
// an event id of this same script, and on or off.
int do_asks(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t ask_uses = 0, ask_strings = 0, ask_steps = 0, ask_echo = 0;
    std::size_t jump_uses = 0, jump_values = 0, jump_steps = 0;
    std::size_t switch_uses = 0, switch_events = 0, switch_flags = 0, switch_global = 0,
                switch_zero = 0;
    world::MapScript global_script;
    if (icons.payload("GLOBAL.EVT", raw) == lod::LodArchive::PayloadError::None) {
        (void)world::MapScript::parse(raw, global_script);
    }
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        world::MapStrings strings;
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        const auto step_of = [&script](std::uint16_t event, std::uint8_t sequence) {
            for (const auto& s : script.event(event)) {
                if (s.sequence == sequence && s.opcode != world::kOpcodeHeader) {
                    return true;
                }
            }
            return false;
        };
        const auto u32_at = [](const std::vector<std::uint8_t>& a, std::size_t at) {
            std::uint32_t value = 0;
            for (int i = 3; i >= 0; --i) {
                value = (value << 8) | a[at + static_cast<std::size_t>(i)];
            }
            return value;
        };
        for (const auto& step : script.steps()) {
            const auto& a = step.arguments;
            if (step.opcode == 26 && a.size() >= 13) {
                ++ask_uses;
                const std::uint32_t prompt = u32_at(a, 0);
                const std::uint32_t first = u32_at(a, 4);
                const std::uint32_t second = u32_at(a, 8);
                const bool resolve = prompt < strings.size() && first < strings.size() &&
                                     second < strings.size();
                ask_strings += resolve ? 1 : 0;
                ask_steps += step_of(step.event_id, a[12]) ? 1 : 0;
                if (resolve) {
                    // The two answers as one word: equal, or apart only by
                    // case or an article.
                    std::string low1, low2;
                    for (const char c : strings.at(first)) {
                        low1 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    for (const char c : strings.at(second)) {
                        low2 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    ask_echo += low1 == low2 || low2.find(low1) != std::string::npos ||
                                        low1.find(low2) != std::string::npos
                                    ? 1
                                    : 0;
                    std::cout << stem << " " << step.event_id << ": \""
                              << strings.at(prompt) << "\" -> \"" << strings.at(first)
                              << "\" / \"" << strings.at(second) << "\"\n";
                }
            } else if (step.opcode == 25 && a.size() >= 6) {
                ++jump_uses;
                for (std::size_t i = 0; i < 6; ++i) {
                    if (a[i] == 0) {
                        continue;
                    }
                    ++jump_values;
                    jump_steps += step_of(step.event_id, a[i]) ? 1 : 0;
                }
            } else if (step.opcode == 32 && a.size() >= 5) {
                ++switch_uses;
                const auto id = static_cast<std::uint16_t>(u32_at(a, 0));
                const bool defined = script.defines(id);
                switch_events += defined ? 1 : 0;
                switch_zero += id == 0 ? 1 : 0;
                if (!defined && id != 0) {
                    switch_global += global_script.defines(id) ? 1 : 0;
                    if (!global_script.defines(id)) {
                        std::cout << "op32 miss: " << stem << " " << step.event_id << " -> "
                                  << id << "\n";
                    }
                }
                switch_flags += a[4] <= 1 ? 1 : 0;
            }
        }
    }
    std::cout << "op26 [prompt u32][answer u32][answer u32][step u8]: " << ask_uses
              << " full uses; all three strings resolve on " << ask_strings
              << ", the step exists on " << ask_steps << ", the answers echo on " << ask_echo
              << "\n";
    std::cout << "op25 [step u8 x6, zero-padded]: " << jump_uses << " uses, " << jump_values
              << " nonzero entries, " << jump_steps << " are steps of their own event\n";
    std::cout << "op32 [event u32][on/off u8]: " << switch_uses
              << " uses; the id is an event of this script on " << switch_events
              << ", of GLOBAL.EVT on another " << switch_global << ", zero on " << switch_zero
              << "; the byte is 0/1 on " << switch_flags << "\n";
    return 0;
}

// Research mode: the give/take/set types still unnamed, joined against the
// numbers their own events speak. A quest's reward step and its reward prose
// sit in the same event; when the prose says "500 experience" and a give of
// an unnamed type carries 500, the type has told its name.
int do_currencies(const starhaven::lod::LodArchive& icons,
                  const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::NpcDialogueTable dialogue;
    (void)data::load_npc_dialogue(data_dir, dialogue);

    // type -> word spoken with a matching number -> how often.
    std::map<int, std::map<std::string, std::size_t>> matches;
    std::map<int, std::size_t> uses;

    std::span<const std::byte> raw;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        world::MapStrings strings;
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        const bool global = stem == "GLOBAL";

        std::uint16_t last = 0xFFFF;
        for (std::size_t i = 0; i < script.steps().size(); ++i) {
            if (script.steps()[i].event_id == last) {
                continue;
            }
            last = script.steps()[i].event_id;
            const auto steps = script.event(last);

            // Everything this event says, in one string.
            std::string prose;
            for (const auto& step : steps) {
                if ((step.opcode != world::kOpcodeMessage &&
                     step.opcode != world::kOpcodeLongMessage) ||
                    step.arguments.empty()) {
                    continue;
                }
                const int index = step.arguments.front();
                if (global) {
                    if (const auto* row = dialogue.at(index); row != nullptr) {
                        prose += row->text + " ";
                    }
                } else if (static_cast<std::size_t>(index) < strings.size()) {
                    prose += std::string(strings.at(static_cast<std::size_t>(index))) + " ";
                }
            }

            // The numbers it speaks, each with the word that follows.
            std::vector<std::pair<long, std::string>> spoken;
            for (std::size_t at = 0; at < prose.size(); ++at) {
                if (std::isdigit(static_cast<unsigned char>(prose[at])) == 0) {
                    continue;
                }
                long value = 0;
                std::size_t p = at;
                while (p < prose.size() &&
                       (std::isdigit(static_cast<unsigned char>(prose[p])) != 0 ||
                        (prose[p] == ',' && p + 1 < prose.size() &&
                         std::isdigit(static_cast<unsigned char>(prose[p + 1])) != 0))) {
                    if (prose[p] != ',') {
                        value = value * 10 + (prose[p] - '0');
                    }
                    ++p;
                }
                while (p < prose.size() && prose[p] == ' ') {
                    ++p;
                }
                std::string word;
                while (p < prose.size() &&
                       std::isalpha(static_cast<unsigned char>(prose[p])) != 0) {
                    word += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(prose[p])));
                    ++p;
                }
                spoken.emplace_back(value, word);
                at = p;
            }

            // The unnamed types this event gives, takes or sets.
            for (const auto& step : steps) {
                if ((step.opcode != world::kOpcodeGive && step.opcode != world::kOpcodeTake &&
                     step.opcode != world::kOpcodeSet) ||
                    step.arguments.size() < 5) {
                    continue;
                }
                const int type = step.arguments[0];
                if (type == world::kVarQuestBit || type == world::kVarItem ||
                    type == world::kVarGold) {
                    continue;
                }
                std::uint32_t value = 0;
                for (std::size_t b = 4; b >= 1; --b) {
                    value = (value << 8) | step.arguments[b];
                }
                ++uses[type];
                for (const auto& [number, word] : spoken) {
                    if (number == static_cast<long>(value) && !word.empty()) {
                        ++matches[type][word];
                    }
                }
            }
        }
    }

    // Type 12 against `Awards.txt`: the range fits the filled rows, and the
    // one value a known quest sets — Goblinwatch's 53 — names exactly
    // "Solved the Goblinwatch Combination". Count the whole join.
    {
        data::JournalTable awards;
        if (data::load_awards(data_dir, awards) == data::GameDataError::None) {
            std::set<int> filled;
            for (const auto& row : awards.entries()) {
                if (row.has_text()) {
                    filled.insert(row.bit);
                }
            }
            std::size_t uses = 0, hits = 0;
            std::map<int, std::size_t> misses;
            std::size_t fame_uses = 0;
            long fame_min = 0, fame_max = 0;
            for (const auto& entry : icons.entries()) {
                if (!is_script(entry.name)) {
                    continue;
                }
                world::MapScript script;
                if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
                    world::MapScript::parse(raw, script) != world::MapScriptError::None) {
                    continue;
                }
                for (const auto& step : script.steps()) {
                    const bool typed = step.opcode == world::kOpcodeCheck ||
                                       step.opcode == world::kOpcodeGive ||
                                       step.opcode == world::kOpcodeTake ||
                                       step.opcode == world::kOpcodeSet;
                    if (!typed || step.arguments.size() < 5) {
                        continue;
                    }
                    std::uint32_t value = 0;
                    for (std::size_t b = 4; b >= 1; --b) {
                        value = (value << 8) | step.arguments[b];
                    }
                    if (step.arguments[0] == 12) {
                        ++uses;
                        if (filled.contains(static_cast<int>(value))) {
                            ++hits;
                        } else {
                            ++misses[static_cast<int>(value)];
                        }
                    } else if (step.arguments[0] == 22) {
                        ++fame_uses;
                        fame_min = fame_uses == 1
                                       ? static_cast<long>(value)
                                       : std::min(fame_min, static_cast<long>(value));
                        fame_max = std::max(fame_max, static_cast<long>(value));
                    }
                }
            }
            std::cout << "type 12 vs Awards.txt filled rows: " << hits << "/" << uses;
            for (const auto& [value, count] : misses) {
                std::cout << " (miss " << value << " x" << count << ")";
            }
            std::cout << "\ntype 22: " << fame_uses << " uses, values " << fame_min << ".."
                      << fame_max << "\n";
        }
    }

    for (const auto& [type, words] : matches) {
        std::cout << "type " << type << " (" << uses[type] << " uses):";
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [word, count] : words) {
            ranked.emplace_back(count, word);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::size_t shown = 0;
        for (const auto& [count, word] : ranked) {
            if (++shown > 6) {
                break;
            }
            std::cout << " " << word << " x" << count;
        }
        std::cout << "\n";
    }
    return 0;
}

// Verification mode: the New Sorpigal opening arc, walked end to end
// through the same code the game runs. Exits nonzero on the first beat
// that fails, so it can stand as a regression against a real install.
int do_arc(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;
    namespace game = starhaven::game;

    std::span<const std::byte> raw;
    world::MapScript global;
    if (icons.payload("GLOBAL.EVT", raw) != lod::LodArchive::PayloadError::None ||
        world::MapScript::parse(raw, global) != world::MapScriptError::None) {
        std::cerr << "arc: no GLOBAL.EVT\n";
        return 1;
    }
    std::size_t passed = 0;
    const auto beat = [&passed](bool ok, const char* what) {
        std::cout << (ok ? "  ok  " : "  FAIL") << "  " << what << "\n";
        if (ok) {
            ++passed;
        }
        return ok;
    };

    // 1. The letter refused without the letter.
    {
        game::WalkState state;
        state.bits.insert(81);
        const auto outcome = game::walk_event(global, 1, state);
        if (!beat(outcome.ran && state.gold == 0 && outcome.taken.empty(),
                  "Andover only talks about the letter until it is held")) {
            return 1;
        }
    }
    // 2. The delivery pays, rotates the topic and moves the bits.
    {
        game::WalkState state;
        state.bits.insert(81);
        state.items.push_back(505);
        const auto outcome = game::walk_event(global, 1, state);
        // The event never takes the letter — its steps hold no item-take —
        // so the scroll stays a keepsake; only the bits and the topic move.
        const bool ok = outcome.ran && state.gold == 1000 &&
                        state.npc_topics.at({1, 0}) == 2 && !state.bits.contains(81) &&
                        state.bits.contains(82) &&
                        std::find(state.items.begin(), state.items.end(), 505) !=
                            state.items.end();
        if (!beat(ok, "the letter pays 1000, rotates Andover and moves bit 81 to 82")) {
            return 1;
        }
    }
    // 3. Goblinwatch's reward: the combination scroll earns the award, the
    //    gold and the experience.
    {
        game::WalkState state;
        state.items.push_back(543);
        const auto outcome = game::walk_event(global, 4, state);
        // This reward, too, checks its item without taking it.
        const bool ok = outcome.ran && state.awards.contains(53) && state.gold == 2000 &&
                        state.experience == 2000;
        if (!beat(ok, "the combination scroll earns award 53, 2000 gold and 2000 experience")) {
            return 1;
        }
    }
    // 4. Goblinwatch's levers throw doors once, and hold after.
    {
        world::MapScript d01;
        if (icons.payload("D01.EVT", raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, d01) != world::MapScriptError::None) {
            std::cerr << "arc: no D01.EVT\n";
            return 1;
        }
        // A plain door opens every use; a lever is the event that checks
        // its own variable before throwing — the first with both steps.
        std::uint16_t lever = 0;
        for (const auto& step : d01.steps()) {
            if (step.opcode != world::kOpcodeDoor) {
                continue;
            }
            bool gated = false;
            for (const auto& other : d01.event(step.event_id)) {
                gated = gated || other.opcode == world::kOpcodeCheck;
            }
            if (gated) {
                lever = step.event_id;
                break;
            }
        }
        game::WalkState state;
        const auto first = game::walk_event(d01, lever, state);
        const auto second = game::walk_event(d01, lever, state);
        const bool ok = lever != 0 && !first.doors.empty() && second.doors.empty();
        if (!beat(ok, "a Goblinwatch lever throws its doors once and holds")) {
            return 1;
        }
    }
    // 5. The coach at New Sorpigal reaches Castle Ironfist for a fare.
    {
        data::BuildingStatsTable buildings;
        data::MapStatsTable maps;
        if (data::load_building_stats(data_dir, buildings) != data::GameDataError::None ||
            data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
            std::cerr << "arc: no tables\n";
            return 1;
        }
        bool reaches = false;
        int fare = 0;
        for (const auto* shop : buildings.on_map("E3")) {
            if (!game::is_travel(*shop)) {
                continue;
            }
            for (const auto& route : game::routes_of(*shop, maps)) {
                if (route.map_file == "OutD3.Odm") {
                    reaches = true;
                    fare = game::fare_of(*shop);
                }
            }
        }
        if (!beat(reaches && fare > 0, "a New Sorpigal coach is priced through to Ironfist")) {
            return 1;
        }
    }
    // 6. The Fire guild's shelves and roll both resolve.
    {
        data::JournalTable awards;
        if (data::load_awards(data_dir, awards) != data::GameDataError::None) {
            std::cerr << "arc: no Awards.txt\n";
            return 1;
        }
        const bool ok = game::guild_award_of(data::SpellSchool::Fire, awards) == 74;
        if (!beat(ok, "the Fire guild's membership is award 74, Joined the Fire Guild")) {
            return 1;
        }
    }
    // 7. The Seer points at each stage in the chain's own words.
    {
        game::WalkState state;
        state.bits.insert(81);
        const auto before = game::walk_event(global, 46, state);
        game::WalkState later;
        later.bits.insert(82);
        const auto after = game::walk_event(global, 46, later);
        // npctext.txt rows 436 and 428: "show The Letter to Andover
        // Potbello", then "give The Letter to Regent Wilbur Humphrey in
        // Ironfist Castle" — the ids are one past the row, the bank's own
        // 1-based habit.
        const bool ok =
            std::find(before.said.begin(), before.said.end(), 435) != before.said.end() &&
            std::find(after.said.begin(), after.said.end(), 427) != after.said.end();
        if (!beat(ok, "the Seer names Andover first and then Regent Humphrey")) {
            return 1;
        }
    }
    // 8. The letter reaches Humphrey, and this delivery takes it.
    {
        game::WalkState state;
        state.bits.insert(82);
        state.items.push_back(505);
        const auto outcome = game::walk_event(global, 9, state);
        const bool ok = outcome.ran && state.gold == 5000 && state.experience == 3000 &&
                        state.awards.contains(58) && !state.bits.contains(82) &&
                        std::find(state.items.begin(), state.items.end(), 505) ==
                            state.items.end() &&
                        state.npc_topics.at({4, 0}) == 10;
        if (!beat(ok, "Humphrey pays 5000 and 3000 experience, takes the letter, "
                      "grants award 58")) {
            return 1;
        }
    }
    // 9. Humphrey's little detail: the first council task opens.
    {
        game::WalkState state;
        const auto outcome = game::walk_event(global, 10, state);
        const bool ok =
            outcome.ran && state.bits.contains(86) && state.npc_topics.at({4, 0}) == 11;
        if (!beat(ok, "his next word sets bit 86 and turns to the shield topic")) {
            return 1;
        }
    }
    // 10. Lord Kilburn's shield closes the first council quest.
    {
        game::WalkState state;
        state.bits.insert(86);
        state.items.push_back(499);
        const auto outcome = game::walk_event(global, 11, state);
        const bool ok = outcome.ran && state.gold == 5000 && state.experience == 40000 &&
                        state.awards.contains(2) && !state.bits.contains(86) &&
                        std::find(state.items.begin(), state.items.end(), 499) ==
                            state.items.end() &&
                        state.npc_topics.at({4, 0}) == 12;
        if (!beat(ok, "the shield pays 5000 and 40000 experience and grants award 2, "
                      "the first council seal")) {
            return 1;
        }
    }
    // 11. Albert Newton takes the Hourglass of Time.
    {
        game::WalkState state;
        state.items.push_back(433);
        const auto outcome = game::walk_event(global, 52, state);
        const bool ok = outcome.ran && state.experience == 50000 && state.awards.contains(3) &&
                        std::find(state.items.begin(), state.items.end(), 433) ==
                            state.items.end() &&
                        state.npc_topics.at({5, 0}) == 54;
        if (!beat(ok, "the Hourglass pays 50000 experience and seals award 3")) {
            return 1;
        }
    }
    // 12. Osric Temper hears the Devil's Post has fallen.
    {
        game::WalkState state;
        state.items.push_back(506);
        const auto outcome = game::walk_event(global, 62, state);
        const bool ok = outcome.ran && state.experience == 40000 && state.awards.contains(4) &&
                        std::find(state.items.begin(), state.items.end(), 506) ==
                            state.items.end() &&
                        state.npc_topics.at({6, 0}) == 64;
        if (!beat(ok, "the Devil's Post proof pays 40000 experience and seals award 4")) {
            return 1;
        }
    }
    // 13. Anthony Stone takes the Prince of Thieves off the party's hands:
    //     the check is variable type 214 against 17, and NPCdata.txt row 17
    //     is the Prince himself — the type reads as "this person follows".
    {
        game::WalkState state;
        state.variables[214] = 17;
        const auto outcome = game::walk_event(global, 33, state);
        const bool ok = outcome.ran && state.gold == 10000 && state.experience == 30000 &&
                        state.awards.contains(5) && state.npc_topics.at({16, 0}) == 34;
        if (!beat(ok, "the Prince delivered pays 10000 gold, 30000 experience, award 5")) {
            return 1;
        }
    }
    // 14. Loretta Fleise pays for the stable prices, bit 117's errand.
    {
        game::WalkState state;
        state.bits.insert(117);
        const auto outcome = game::walk_event(global, 80, state);
        const bool ok = outcome.ran && state.gold == 25000 && state.experience == 25000 &&
                        state.awards.contains(6) && state.npc_topics.at({14, 0}) == 81;
        if (!beat(ok, "the stable prices pay 25000 and 25000 experience, award 6")) {
            return 1;
        }
    }
    // 15. Erik Von Stromgard's winter ends the moment he is told.
    {
        game::WalkState state;
        const auto outcome = game::walk_event(global, 89, state);
        const bool ok = outcome.ran && state.experience == 50000 && state.awards.contains(7) &&
                        state.bits.contains(175) && state.npc_topics.at({15, 0}) == 90;
        if (!beat(ok, "ending winter pays 50000 experience and seals award 7")) {
            return 1;
        }
    }
    // 16. The last seal closes the ladder: Humphrey's own event checks the
    //     whole council — award 32, the exposed traitor, among them — and
    //     sets bit 167 when every seal is in.
    {
        game::WalkState state;
        state.items.push_back(499);
        for (const int held : {32, 3, 4, 5, 6, 7}) {
            state.awards.insert(held);
        }
        const auto outcome = game::walk_event(global, 11, state);
        const bool ok = outcome.ran && state.awards.contains(2) && state.bits.contains(167);
        if (!beat(ok, "with every seal and the traitor exposed, bit 167 opens the next act")) {
            return 1;
        }
    }
    std::cout << passed << " beats of the opening arc hold\n";
    return 0;
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
    if (stem == "--scan") {
        return do_scan(icons);
    }
    if (stem == "--transitions") {
        return do_transitions(icons, *install / "data");
    }
    if (stem == "--asks") {
        return do_asks(icons);
    }
    if (stem == "--currencies") {
        return do_currencies(icons, *install / "data");
    }
    if (stem == "--arc") {
        return do_arc(icons, *install / "data");
    }
    if (stem == "--launches") {
        return do_launches(icons);
    }
    if (stem == "--projectiles") {
        return do_projectiles(icons);
    }
    if (stem == "--catalog" && argc == 3) {
        return do_catalog(icons, std::atoi(argv[2]));
    }
    if (stem == "--npc-mutations") {
        return do_npc_mutations(icons, *install / "data");
    }
    if (stem == "--headers") {
        return do_headers(icons);
    }
    if (stem == "--soundsweep") {
        return do_soundsweep(icons);
    }
    if (stem == "--unheaded") {
        return do_unheaded(icons);
    }
    if (stem == "--textures") {
        lod::LodArchive bitmaps;
        if (lod::LodArchive::open(*install / "data" / "BITMAPS.LOD", bitmaps) !=
            lod::LodError::None) {
            std::cerr << "error: could not open BITMAPS.LOD\n";
            return 1;
        }
        return do_sounds(icons, bitmaps);
    }
    if (stem == "--out") {
        return do_out(icons, argc == 3 ? std::string(argv[2]) + ".EVT" : "OUT.EVT");
    }
    if (stem == "--variables") {
        return do_variables(icons);
    }
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
    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (!strings.at(i).empty()) {
            std::cout << "  [" << i << "] " << strings.at(i) << "\n";
        }
    }
    return 0;
}
