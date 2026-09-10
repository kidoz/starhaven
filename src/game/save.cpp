#include "game/save.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "game/script_walk.hpp"

namespace starhaven::game {
namespace {

template <typename Number> Number parse_number(std::string_view cell) {
    Number value{};
    const auto result = std::from_chars(cell.data(), cell.data() + cell.size(), value);
    if (result.ec != std::errc{} || result.ptr != cell.data() + cell.size()) {
        throw std::invalid_argument("invalid save number");
    }
    if constexpr (std::is_floating_point_v<Number>) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("non-finite save coordinate");
        }
    }
    return value;
}

template <typename Number> Number next_number(std::istringstream& fields) {
    std::string cell;
    if (!std::getline(fields, cell, '\t')) {
        // Only appended fields may be absent: valid_record_shape checks
        // each record's original required prefix before decoding it.
        return Number{};
    }
    return parse_number<Number>(cell);
}

int next_count(std::istringstream& fields) {
    if (fields.peek() == std::char_traits<char>::eof()) {
        throw std::invalid_argument("missing save collection count");
    }
    const int count = next_number<int>(fields);
    const auto position = fields.tellg();
    const std::string remaining =
        position < 0 ? std::string{} : fields.str().substr(static_cast<std::size_t>(position));
    const auto available =
        remaining.empty() ? 0 : 1 + std::count(remaining.begin(), remaining.end(), '\t');
    if (count < 0 || count > available) {
        throw std::invalid_argument("invalid save collection count");
    }
    return count;
}

bool valid_record_shape(std::string_view kind, std::string_view line) {
    // Field counts include the record kind. Optional trailing fields were
    // appended to version 1 over time; the original prefixes stay required.
    static constexpr auto kMinimumFields = std::to_array<std::pair<std::string_view, int>>({
        {"map", 2},         {"camera", 6},    {"clock", 2},     {"gold", 2},     {"bank", 2},
        {"award", 2},       {"visited", 2},   {"fly", 2},       {"standing", 3}, {"reputation", 2},
        {"quick", 5},       {"readied", 5},   {"turnbased", 3}, {"torch", 2},    {"eye", 3},
        {"beacon", 6},      {"hired", 4},     {"wageday", 2},   {"recall", 6},   {"note", 2},
        {"bit", 2},         {"resolved", 2},  {"disabled", 3},  {"var", 3},      {"npctopic", 4},
        {"npcplace", 3},    {"chest", 2},     {"door", 2},      {"skill", 4},    {"character", 14},
        {"spells", 2},      {"partybuff", 5}, {"buff", 5},      {"temps", 19},   {"attributes", 9},
        {"resistances", 7}, {"equipped", 12}, {"item", 7},
    });
    const auto count = 1 + std::count(line.begin(), line.end(), '\t');
    for (const auto& [record, minimum] : kMinimumFields) {
        if (kind == record) {
            if (count < minimum) {
                return false;
            }
            std::size_t start = kind.size() + 1;
            for (int field = 1; field < minimum; ++field) {
                const auto end = line.find('\t', start);
                const auto length = (end == std::string_view::npos ? line.size() : end) - start;
                const bool text =
                    kind == "map" || kind == "visited" || (kind == "character" && field >= 12) ||
                    (kind == "hired" && field == 3) || (kind == "skill" && field == 3) ||
                    (kind == "beacon" && field == 5) || (kind == "disabled" && field == 2) ||
                    (kind == "recall" && field == 1);
                if (length == 0 && !text) {
                    return false;
                }
                start = end == std::string_view::npos ? line.size() : end + 1;
            }
            return true;
        }
    }
    return true;  // Forward-compatible extension record.
}

}  // namespace

// Write a state as text. Fields are tab-separated, one record per line, and
// names go last on their lines so their own spaces survive.
std::string save_text(const SaveState& state) {
    std::ostringstream out;
    out << kSaveMagic << "\t" << kSaveVersion << "\n";
    out << "map\t" << state.map_file << "\n";
    out << "camera\t" << state.x << "\t" << state.y << "\t" << state.z << "\t" << state.yaw << "\t"
        << state.pitch << "\n";
    out << "clock\t" << state.minutes << "\n";
    out << "gold\t" << state.gold << "\t" << state.food << "\n";
    out << "bank\t" << state.bank_gold << "\n";
    for (const int award : state.awards) {
        out << "award\t" << award << "\n";
    }
    for (const auto& town : state.visited_towns) {
        out << "visited\t" << town << "\n";
    }
    if (state.fly_until > 0) {
        out << "fly\t" << state.fly_until << "\n";
    }
    if (state.reputation != 0) {
        out << "reputation\t" << state.reputation << "\n";
    }
    if (state.deaths != 0 || state.prison_terms != 0) {
        out << "standing\t" << state.deaths << "\t" << state.prison_terms << "\n";
    }
    if (state.quick[0] != 0 || state.quick[1] != 0 || state.quick[2] != 0 || state.quick[3] != 0) {
        out << "quick\t" << state.quick[0] << "\t" << state.quick[1] << "\t" << state.quick[2]
            << "\t" << state.quick[3] << "\n";
    }
    if (state.readied[0] != 0 || state.readied[1] != 0 || state.readied[2] != 0 ||
        state.readied[3] != 0) {
        out << "readied\t" << state.readied[0] << "\t" << state.readied[1] << "\t"
            << state.readied[2] << "\t" << state.readied[3] << "\n";
    }
    if (state.turn_based || state.hourglass_turn > 0) {
        out << "turnbased\t" << (state.turn_based ? 1 : 0) << "\t" << state.hourglass_turn << "\n";
    }
    if (state.torch_until > 0) {
        out << "torch\t" << state.torch_until << "\n";
    }
    if (state.eye_until > 0) {
        out << "eye\t" << state.eye_until << "\t" << state.eye_rank << "\n";
    }
    for (const auto& beacon : state.beacons) {
        out << "beacon\t" << beacon.until << "\t" << beacon.x << "\t" << beacon.y << "\t"
            << beacon.z << "\t" << beacon.map << "\n";
    }
    for (const auto& h : state.hired) {
        out << "hired\t" << h.npc_id << "\t" << h.profession_id << "\t" << h.name << "\n";
    }
    if (state.wage_day > 0) {
        out << "wageday\t" << state.wage_day << "\n";
    }
    for (const int bit : state.bits) {
        out << "bit\t" << bit << "\n";
    }
    for (const int bit : state.resolved_quests) {
        out << "resolved\t" << bit << "\n";
    }
    for (const auto& [script, events] : state.disabled_events) {
        for (const int event : events) {
            out << "disabled\t" << event << "\t" << script << "\n";
        }
    }
    for (const int note : state.autonotes) {
        out << "note\t" << note << "\n";
    }
    for (const auto& map : state.remembered) {
        out << "recall\t" << map.file << "\t" << map.day << "\t" << map.opened_chests.size();
        for (const int chest : map.opened_chests) {
            out << "\t" << chest;
        }
        out << "\t" << map.open_doors.size();
        for (const std::uint32_t door : map.open_doors) {
            out << "\t" << door;
        }
        out << "\t" << map.dead.size();
        for (const std::size_t actor : map.dead) {
            out << "\t" << actor;
        }
        out << "\n";
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
    // The party's own sixteen, before the characters.
    for (std::size_t slot = 0; slot < kPartyBuffCount; ++slot) {
        if (state.party_buffs.until(slot) != 0) {
            out << "partybuff\t" << slot << "\t" << state.party_buffs.until(slot) << "\t"
                << state.party_buffs.raw_power(slot) << "\t" << state.party_buffs.raw_skill(slot)
                << "\n";
        }
    }
    for (std::size_t i = 0; i < state.party.size(); ++i) {
        const Character& who = state.party[i];
        out << "character\t" << i << "\t" << who.face << "\t" << who.level << "\t" << who.experience
            << "\t" << who.age << "\t" << who.hit_points << "\t" << who.max_hit_points << "\t"
            << who.spell_points << "\t" << who.max_spell_points << "\t" << who.armor_class << "\t"
            << who.skill_points << "\t" << who.name << "\t" << who.class_name << "\n";
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
        // The worn enchantments appended in order, so older saves read.
        for (const int bonus : who.worn_standard) {
            out << "\t" << bonus;
        }
        for (const int strength : who.worn_strength) {
            out << "\t" << strength;
        }
        for (const int special : who.worn_special) {
            out << "\t" << special;
        }
        for (const int charges : who.worn_charges) {
            out << "\t" << charges;
        }
        out << "\n";
        if (!who.known_spells.empty()) {
            out << "spells\t" << i;
            for (const int id : who.known_spells) {
                out << "\t" << id;
            }
            out << "\n";
        }
        for (const auto& [skill, points] : who.skills) {
            out << "skill\t" << i << "\t" << points << "\t" << skill << "\n";
        }
        // The character's sixteen buff slots, one record each; an empty slot
        // is skipped, so a party with no spells up costs nothing.
        for (std::size_t slot = 0; slot < kCharacterBuffCount; ++slot) {
            if (who.buffs.until(slot) != 0) {
                out << "buff\t" << i << "\t" << slot << "\t" << who.buffs.until(slot) << "\t"
                    << who.buffs.raw_power(slot) << "\n";
            }
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
        out << "\t" << who.poisoned_minute << "\t" << who.diseased_minute << "\t"
            << who.affliction_minute;
        out << "\n";
        for (const auto& item : state.packs[i]) {
            out << "item\t" << i << "\t" << item.item_id << "\t" << item.x << "\t" << item.y << "\t"
                << item.width << "\t" << item.height << "\t" << (item.identified ? 1 : 0) << "\t"
                << item.standard_bonus << "\t" << item.standard_strength << "\t"
                << item.special_bonus << "\t" << item.charges << "\n";
        }
    }
    out << "end\n";
    return out.str();
}

// Read one back. Unknown record kinds are skipped, so a newer save loads as
// far as an older engine understands it; a wrong magic or version refuses.
static bool parse_save_data(std::string_view text, SaveState& out) {
    out = {};
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) {
        return false;
    }
    int version = 0;
    {
        std::istringstream head(line);
        std::string magic;
        head >> magic >> version;
        if (magic != kSaveMagic || version < kOldestSaveVersion || version > kSaveVersion) {
            return false;
        }
        std::string extra;
        if (head >> extra) {
            return false;
        }
    }
    std::set<std::string> required;
    std::set<std::size_t> characters;
    bool ended = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "end") {
            ended = true;
            break;
        }
        std::istringstream fields(line);
        std::string kind;
        std::getline(fields, kind, '\t');
        if (!valid_record_shape(kind, line)) {
            return false;
        }
        if ((kind == "map" || kind == "camera" || kind == "clock" || kind == "gold") &&
            !required.insert(kind).second) {
            return false;
        }
        const auto next_int = [&fields] { return next_number<int>(fields); };
        const auto next_time = [&fields] { return next_number<std::int64_t>(fields); };
        const auto next_float = [&fields] { return next_number<float>(fields); };
        if (kind == "map") {
            std::getline(fields, out.map_file, '\t');
        } else if (kind == "camera") {
            out.x = next_float();
            out.y = next_float();
            out.z = next_float();
            out.yaw = next_float();
            out.pitch = next_float();
        } else if (kind == "clock") {
            out.minutes = next_time();
        } else if (kind == "gold") {
            out.gold = next_int();
            out.food = next_int();  // appended later; an old save reads 0
        } else if (kind == "bank") {
            out.bank_gold = next_int();
        } else if (kind == "award") {
            out.awards.push_back(next_int());
        } else if (kind == "visited") {
            std::string town;
            std::getline(fields, town, '\t');
            if (!town.empty()) {
                out.visited_towns.push_back(std::move(town));
            }
        } else if (kind == "fly") {
            out.fly_until = next_time();
        } else if (kind == "standing") {
            out.deaths = next_int();
            out.prison_terms = next_int();
        } else if (kind == "reputation") {
            out.reputation = next_int();
        } else if (kind == "quick") {
            for (auto& id : out.quick) {
                id = next_int();
            }
        } else if (kind == "readied") {
            for (auto& id : out.readied) {
                id = next_int();
            }
        } else if (kind == "turnbased") {
            out.turn_based = next_int() != 0;
            out.hourglass_turn = next_int();
        } else if (kind == "torch") {
            out.torch_until = next_time();
        } else if (kind == "eye") {
            out.eye_until = next_time();
            out.eye_rank = next_int();
        } else if (kind == "beacon") {
            SaveState::Beacon beacon;
            beacon.until = next_time();
            beacon.x = next_float();
            beacon.y = next_float();
            beacon.z = next_float();
            std::getline(fields, beacon.map, '\t');
            if (!beacon.map.empty()) {
                out.beacons.push_back(std::move(beacon));
            }
        } else if (kind == "hired") {
            SaveState::Hired h;
            h.npc_id = next_int();
            h.profession_id = next_int();
            std::getline(fields, h.name, '\t');
            out.hired.push_back(std::move(h));
        } else if (kind == "wageday") {
            out.wage_day = next_time();
        } else if (kind == "recall") {
            const auto next_text = [&fields] {
                std::string cell;
                std::getline(fields, cell, '\t');
                return cell;
            };
            SaveState::RememberedMap map;
            map.file = next_text();
            map.day = next_time();
            for (int left = next_count(fields); left > 0; --left) {
                map.opened_chests.insert(next_int());
            }
            for (int left = next_count(fields); left > 0; --left) {
                map.open_doors.push_back(next_number<std::uint32_t>(fields));
            }
            for (int left = next_count(fields); left > 0; --left) {
                map.dead.push_back(next_number<std::size_t>(fields));
            }
            if (!map.file.empty()) {
                out.remembered.push_back(std::move(map));
            }
        } else if (kind == "resolved") {
            out.resolved_quests.insert(next_int());
        } else if (kind == "disabled") {
            const int event = next_int();
            std::string script;
            std::getline(fields, script, '\t');
            out.disabled_events[script_scope(script)].insert(event);
        } else if (kind == "note") {
            out.autonotes.insert(next_int());
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
            out.open_doors.push_back(next_number<std::uint32_t>(fields));
        } else if (kind == "skill") {
            const int member = next_int();
            const int points = next_int();
            std::string name;
            std::getline(fields, name, '\t');
            if (member >= 0 && member < 4 && !name.empty()) {
                out.party[static_cast<std::size_t>(member)].skills[name] = points;
            }
        } else if (kind == "character") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size() || !characters.insert(i).second) {
                return false;
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
                return false;
            }
            std::string cell;
            while (std::getline(fields, cell, '\t')) {
                if (!cell.empty()) {
                    out.party[i].known_spells.insert(parse_number<int>(cell));
                }
            }
        } else if (kind == "partybuff") {
            const auto slot = static_cast<std::size_t>(next_int());
            const std::int64_t until = next_time();
            const int power = next_int();
            const int skill = next_int();
            out.party_buffs.cast(static_cast<int>(slot), until, power, skill);
        } else if (kind == "buff") {
            const auto i = static_cast<std::size_t>(next_int());
            const auto slot = static_cast<std::size_t>(next_int());
            const std::int64_t until = next_time();
            const int power = next_int();
            if (i < out.party.size()) {
                out.party[i].buffs.cast(slot, until, power);
            }
        } else if (kind == "temps") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                return false;
            }
            Character& who = out.party[i];
            who.temp_armor = next_int();
            who.haste_until = next_time();
            who.bless_until = next_time();
            who.heroism_until = next_time();
            who.stone_skin_until = next_time();
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
            who.poisoned_minute = next_time();
            who.diseased_minute = next_time();
            who.affliction_minute = next_time();
        } else if (kind == "attributes" || kind == "resistances" || kind == "equipped") {
            const auto i = static_cast<std::size_t>(next_int());
            if (i >= out.party.size()) {
                return false;
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
                for (auto& bonus : who.worn_standard) {
                    bonus = next_int();
                }
                for (auto& strength : who.worn_strength) {
                    strength = next_int();
                }
                for (auto& special : who.worn_special) {
                    special = next_int();
                }
                for (auto& charges : who.worn_charges) {
                    charges = next_int();
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
            // Appended later; an old save's blank cell reads 0, and its
            // items were all known when they were written.
            std::string flag;
            std::getline(fields, flag, '\t');
            const int identified = flag.empty() ? 1 : parse_number<int>(flag);
            if (identified != 0 && identified != 1) {
                return false;
            }
            item.identified = identified != 0;
            item.standard_bonus = next_int();
            item.standard_strength = next_int();
            item.special_bonus = next_int();
            item.charges = next_int();
            out.packs[i].push_back(item);
        }
    }
    // No records may follow the terminator, apart from blank lines.
    while (ended && std::getline(in, line)) {
        if (!line.empty() && line != "\r") {
            return false;
        }
    }
    return !out.map_file.empty() && required.size() == 4 && characters.size() == 4 &&
           (version == 1 || ended);
}

bool parse_save(std::string_view text, SaveState& out) {
    SaveState candidate;
    try {
        if (!parse_save_data(text, candidate)) {
            return false;
        }
    } catch (const std::invalid_argument&) {
        return false;
    }
    out = std::move(candidate);
    return true;
}

}  // namespace starhaven::game
