#ifndef STARHAVEN_GAME_SAVE_HPP
#define STARHAVEN_GAME_SAVE_HPP

// Saving the game, and getting it back.
//
// The format is this engine's own and says so: a versioned, line-based text
// file of what the engine actually tracks — quest bits, event variables, the
// purse, the packs, the party, the clock, the map and where the party stands
// on it, and the current map's opened chests and thrown doors. It is not the
// original's save format and does not try to be; nothing here rereads or
// writes the original's files.

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "game/inventory.hpp"
#include "game/party.hpp"

namespace starhaven::game {

inline constexpr int kSaveVersion = 1;
inline constexpr const char* kSaveMagic = "starhaven-save";

// Everything a save holds, in plain values the shell assembles and applies.
struct SaveState {
    std::string map_file;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    std::int64_t minutes = 0;
    int gold = 0;
    int bank_gold = 0;
    int food = 0;

    // The hired help: enough to stand them back up through the profession
    // table, and when their wages next fall due.
    struct Hired {
        int npc_id = 0;
        int profession_id = 0;
        std::string name;
    };
    std::vector<Hired> hired;
    std::int64_t wage_day = 0;
    std::set<int> bits;
    std::map<int, int> variables;
    std::map<std::pair<int, int>, int> npc_topics;
    std::map<int, int> npc_places;
    std::array<Character, 4> party{};
    std::array<std::vector<PackedItem>, 4> packs{};
    std::vector<int> opened_chests;
    std::vector<std::uint32_t> open_doors;
};

// Write a state as text. Fields are tab-separated, one record per line, and
// names go last on their lines so their own spaces survive.
[[nodiscard]] inline std::string save_text(const SaveState& state) {
    std::ostringstream out;
    out << kSaveMagic << "\t" << kSaveVersion << "\n";
    out << "map\t" << state.map_file << "\n";
    out << "camera\t" << state.x << "\t" << state.y << "\t" << state.z << "\t" << state.yaw
        << "\t" << state.pitch << "\n";
    out << "clock\t" << state.minutes << "\n";
    out << "gold\t" << state.gold << "\t" << state.food << "\n";
    out << "bank\t" << state.bank_gold << "\n";
    for (const auto& h : state.hired) {
        out << "hired\t" << h.npc_id << "\t" << h.profession_id << "\t" << h.name << "\n";
    }
    if (state.wage_day > 0) {
        out << "wageday\t" << state.wage_day << "\n";
    }
    for (const int bit : state.bits) {
        out << "bit\t" << bit << "\n";
    }
    for (const auto& [key, value] : state.variables) {
        out << "var\t" << key << "\t" << value << "\n";
    }
    for (const auto& [key, value] : state.npc_topics) {
        out << "npctopic\t" << key.first << "\t" << key.second << "\t" << value << "\n";
    }
    for (const auto& [npc, place] : state.npc_places) {
        out << "npcplace\t" << npc << "\t" << place << "\n";
    }
    for (const int chest : state.opened_chests) {
        out << "chest\t" << chest << "\n";
    }
    for (const std::uint32_t door : state.open_doors) {
        out << "door\t" << door << "\n";
    }
    for (std::size_t i = 0; i < state.party.size(); ++i) {
        const Character& who = state.party[i];
        out << "character\t" << i << "\t" << who.face << "\t" << who.level << "\t"
            << who.experience << "\t" << who.age << "\t" << who.hit_points << "\t"
            << who.max_hit_points << "\t" << who.spell_points << "\t" << who.max_spell_points
            << "\t" << who.armor_class << "\t" << who.skill_points << "\t" << who.name << "\t"
            << who.class_name << "\n";
        out << "attributes\t" << i;
        for (const int a : who.attributes) {
            out << "\t" << a;
        }
        out << "\n";
        out << "resistances\t" << i;
        for (const int r : who.resistances) {
            out << "\t" << r;
        }
        out << "\n";
        out << "equipped\t" << i;
        for (const int id : who.equipped) {
            out << "\t" << id;
        }
        out << "\n";
        if (!who.known_spells.empty()) {
            out << "spells\t" << i;
            for (const int id : who.known_spells) {
                out << "\t" << id;
            }
            out << "\n";
        }
        out << "temps\t" << i << "\t" << who.temp_armor << "\t" << who.haste_until << "\t"
            << who.bless_until << "\t" << who.heroism_until << "\t" << who.stone_skin_until;
        for (const int bonus : who.temp_attributes) {
            out << "\t" << bonus;
        }
        for (const int bonus : who.temp_resistances) {
            out << "\t" << bonus;
        }
        out << "\t" << who.poisoned;  // this and the rest appended in order,
        out << "\t" << who.diseased;  // so older saves still read
        out << "\t" << who.affliction;
        for (const bool broken : who.equipped_broken) {
            out << "\t" << (broken ? 1 : 0);
        }
        out << "\n";
        for (const auto& item : state.packs[i]) {
            out << "item\t" << i << "\t" << item.item_id << "\t" << item.x << "\t" << item.y
                << "\t" << item.width << "\t" << item.height << "\n";
        }
    }
    return out.str();
}

// Read one back. Unknown record kinds are skipped, so a newer save loads as
// far as an older engine understands it; a wrong magic or version refuses.
[[nodiscard]] inline bool parse_save(std::string_view text, SaveState& out) {
    out = {};
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) {
        return false;
    }
    {
        std::istringstream head(line);
        std::string magic;
        int version = 0;
        head >> magic >> version;
        if (magic != kSaveMagic || version != kSaveVersion) {
            return false;
        }
    }
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string kind;
        std::getline(fields, kind, '\t');
        const auto next_int = [&fields]() {
            std::string cell;
            std::getline(fields, cell, '\t');
            return cell.empty() ? 0 : std::stoi(cell);
        };
        const auto next_float = [&fields]() {
            std::string cell;
            std::getline(fields, cell, '\t');
            return cell.empty() ? 0.0f : std::stof(cell);
        };
        if (kind == "map") {
            std::getline(fields, out.map_file, '\t');
        } else if (kind == "camera") {
            out.x = next_float();
            out.y = next_float();
            out.z = next_float();
            out.yaw = next_float();
            out.pitch = next_float();
        } else if (kind == "clock") {
            std::string cell;
            std::getline(fields, cell, '\t');
            out.minutes = cell.empty() ? 0 : std::stoll(cell);
        } else if (kind == "gold") {
            out.gold = next_int();
            out.food = next_int();  // appended later; an old save reads 0
        } else if (kind == "bank") {
            out.bank_gold = next_int();
        } else if (kind == "hired") {
            SaveState::Hired h;
            h.npc_id = next_int();
            h.profession_id = next_int();
            std::getline(fields, h.name, '\t');
            out.hired.push_back(std::move(h));
        } else if (kind == "wageday") {
            out.wage_day = next_int();
        } else if (kind == "bit") {
            out.bits.insert(next_int());
        } else if (kind == "var") {
            const int key = next_int();
            out.variables[key] = next_int();
        } else if (kind == "npctopic") {
            const int npc = next_int();
            const int slot = next_int();
            out.npc_topics[{npc, slot}] = next_int();
        } else if (kind == "npcplace") {
            const int npc = next_int();
            out.npc_places[npc] = next_int();
        } else if (kind == "chest") {
            out.opened_chests.push_back(next_int());
        } else if (kind == "door") {
            out.open_doors.push_back(static_cast<std::uint32_t>(next_int()));
        } else if (kind == "character") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                continue;
            }
            Character& who = out.party[i];
            who.face = next_int();
            who.level = next_int();
            who.experience = next_int();
            who.age = next_int();
            who.hit_points = next_int();
            who.max_hit_points = next_int();
            who.spell_points = next_int();
            who.max_spell_points = next_int();
            who.armor_class = next_int();
            who.skill_points = next_int();
            std::getline(fields, who.name, '\t');
            std::getline(fields, who.class_name, '\t');
        } else if (kind == "spells") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                continue;
            }
            std::string cell;
            while (std::getline(fields, cell, '\t')) {
                if (!cell.empty()) {
                    out.party[i].known_spells.insert(std::stoi(cell));
                }
            }
        } else if (kind == "temps") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                continue;
            }
            Character& who = out.party[i];
            who.temp_armor = next_int();
            who.haste_until = next_int();
            who.bless_until = next_int();
            who.heroism_until = next_int();
            who.stone_skin_until = next_int();
            for (auto& bonus : who.temp_attributes) {
                bonus = next_int();
            }
            for (auto& bonus : who.temp_resistances) {
                bonus = next_int();
            }
            who.poisoned = next_int();
            who.diseased = next_int();
            std::getline(fields, who.affliction, '\t');
            for (auto&& broken : who.equipped_broken) {
                broken = next_int() != 0;
            }
        } else if (kind == "attributes" || kind == "resistances" || kind == "equipped") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                continue;
            }
            Character& who = out.party[i];
            if (kind == "attributes") {
                for (auto& a : who.attributes) {
                    a = next_int();
                }
            } else if (kind == "resistances") {
                for (auto& r : who.resistances) {
                    r = next_int();
                }
            } else {
                for (auto& id : who.equipped) {
                    id = next_int();
                }
            }
        } else if (kind == "item") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.packs.size()) {
                continue;
            }
            PackedItem item;
            item.item_id = next_int();
            item.x = next_int();
            item.y = next_int();
            item.width = next_int();
            item.height = next_int();
            out.packs[i].push_back(item);
        }
    }
    return !out.map_file.empty();
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SAVE_HPP
