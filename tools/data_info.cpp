#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "core/data/building_stats.hpp"
#include "core/data/dice.hpp"
#include "core/data/game_data.hpp"
#include "core/data/interface_strings.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/npc_stats.hpp"
#include "core/data/spell_stats.hpp"
#include "core/data/spell_effects.hpp"
#include "core/data/text_table.hpp"
#include "core/data/treasure.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/monster_list.hpp"

namespace {

using namespace starhaven;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--list | --maps | --monsters [name] | --items [id]"
              << " | --random-items [id] | --standard-bonuses [id]"
              << " | --special-bonuses [id] | --generate-item LEVEL:SEED[:TYPE] | --check"
              << " | <Table.txt>>\n"
              << "\n"
              << "Reads the tab-separated design tables shipped inside your own\n"
              << "legal installation's icons.lod.\n"
              << "\n"
              << "  --list             list the tables and their sizes\n"
              << "  --maps             MapStats.txt as typed rows\n"
              << "  --monsters [name]  MONSTERS.TXT as typed rows, or one monster\n"
              << "  --spells [name]    Spells.txt as typed rows, or one spell\n"
              << "  --buildings [map]  2DEvents.txt, or one map's establishments\n"
              << "  --npcs [name]      NPCdata.txt, or one person\n"
              << "  --professions      npcprof.txt\n"
              << "  --dialogue [id]    npctopic.txt + npctext.txt\n"
              << "  --news             NPCNews.txt\n"
              << "  --quests [bit]     Quests.txt\n"
              << "  --awards           Awards.txt\n"
              << "  --autonotes        Autonote.txt\n"
              << "  --encounters       MapStats encounter slots against MONSTERS.TXT\n"
              << "  --dice             damage notation across MONSTERS.TXT and ITEMS.TXT\n"
              << "  --personalities    npcbtb.txt reactions and phrasing\n"
              << "  --proftext [id]    PROFTEXT.txt, what a hire says on each day\n"
              << "  --strings [id]     GLOBAL.TXT interface words\n"
              << "  --classes          Class.txt\n"
              << "  --stats            stats.txt\n"
              << "  --skills           SkillDes.txt\n"
              << "  --items [id]       ITEMS.TXT as typed rows, or one direct item id\n"
              << "  --random-items [id] RNDITEMS.TXT weights, or one direct item id\n"
              << "  --standard-bonuses [id] STDITEMS.TXT selectors and strength ranges\n"
              << "  --special-bonuses [id] SPCITEMS.TXT one-based selectors\n"
              << "  --generate-item LEVEL:SEED[:TYPE] deterministic item generation\n"
              << "  --check            cross-check MONSTERS.TXT against DMONLIST.BIN\n"
              << "  <Table.txt>        dump one table's cells\n"
              << "  --rows N           limit a dump to N rows\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

// The archive holds art and tables side by side; the tables are the entries
// whose name ends in ".txt".
bool is_table_name(const std::string& name) {
    if (name.size() < 4)
        return false;
    std::string tail = name.substr(name.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".txt";
}

int do_list(const std::filesystem::path& data_dir) {
    lod::LodArchive archive;
    if (lod::LodArchive::open(data_dir / "icons.lod", archive) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return 1;
    }
    int found = 0;
    for (const auto& e : archive.entries()) {
        if (!is_table_name(e.name))
            continue;
        data::TextTable table;
        std::span<const std::byte> bytes;
        if (archive.payload(e.name, bytes) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        std::size_t columns = 0;
        std::string status = "ok";
        if (data::TextTable::parse(bytes, table) != data::TextTableError::None) {
            status = "parse failed";
        } else {
            for (const auto& row : table.rows())
                columns = std::max(columns, row.size());
        }
        std::cout << "  " << e.name << "  stored=" << e.stored_size
                  << "  rows=" << table.row_count() << "  cols=" << columns << "  " << status
                  << "\n";
        ++found;
    }
    std::cout << found << " tables\n";
    return 0;
}

int do_dump(const std::filesystem::path& data_dir, const std::string& name, std::size_t limit) {
    data::TextTable table;
    if (data::load_text_table(data_dir, name, table) != data::GameDataError::None) {
        std::cerr << "error: could not load table: " << name << "\n";
        return 1;
    }
    std::cout << table.name() << ": " << table.row_count() << " rows\n";
    const std::size_t rows = std::min(limit, table.row_count());
    for (std::size_t r = 0; r < rows; ++r) {
        std::cout << r << ":";
        for (const auto& cellText : table.rows()[r]) {
            std::cout << "\t" << data::cp1252_to_utf8(cellText);
        }
        std::cout << "\n";
    }
    if (rows < table.row_count()) {
        std::cout << "... " << (table.row_count() - rows) << " more rows\n";
    }
    return 0;
}

int do_maps(const std::filesystem::path& data_dir) {
    data::MapStatsTable maps;
    if (data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
        std::cerr << "error: could not load MapStats.txt\n";
        return 1;
    }
    std::cout << maps.size() << " maps\n";
    for (const auto& m : maps.entries()) {
        std::cout << "  " << m.id << "\t" << m.file_name << "\t" << data::cp1252_to_utf8(m.name)
                  << "\ttrack=" << m.music_track << "\trefill=" << m.refill_days
                  << "d\tenc=" << m.encounter_percent << "%";
        for (const auto& e : m.monsters) {
            if (!e.empty())
                std::cout << "\t" << e.monster << " (" << e.count << ")";
        }
        if (!m.designer.empty())
            std::cout << "\t[" << m.designer << "]";
        std::cout << "\n";
    }
    return 0;
}

int do_monsters(const std::filesystem::path& data_dir, const std::string& want) {
    data::TextTable table;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", table) != data::GameDataError::None) {
        std::cerr << "error: could not load MONSTERS.TXT\n";
        return 1;
    }
    data::MonsterStatsTable monsters;
    if (data::MonsterStatsTable::parse(table, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: MONSTERS.TXT has an unexpected header\n";
        return 1;
    }

    if (!want.empty()) {
        const auto* m = monsters.find(want);
        if (m == nullptr) {
            std::cerr << "error: no monster named " << want << "\n";
            return 1;
        }
        std::cout << m->picture << " (" << data::cp1252_to_utf8(m->name) << ")\n"
                  << "  level " << m->level << ", " << m->hit_points << " hp, AC " << m->armor_class
                  << ", " << m->experience << " xp\n"
                  << "  ai " << m->ai_type << ", moves " << m->movement << ", speed " << m->speed
                  << (m->flying ? ", flying" : "") << "\n";
        for (const auto& a : m->attacks) {
            if (a.type.empty() || a.type == "0")
                continue;
            std::cout << "  attack " << a.type << " " << a.damage;
            if (!a.missile.empty() && a.missile != "0")
                std::cout << " (" << a.missile << ")";
            std::cout << "\n";
        }
        if (!m->spells.empty() && m->spells != "0")
            std::cout << "  spells " << m->spells << " at " << m->spell_percent << "%\n";
        std::cout << "  resist fire=" << m->resistance(data::Resistance::Fire)
                  << " elec=" << m->resistance(data::Resistance::Electricity)
                  << " cold=" << m->resistance(data::Resistance::Cold)
                  << " pois=" << m->resistance(data::Resistance::Poison)
                  << " phys=" << m->resistance(data::Resistance::Physical)
                  << " magic=" << m->resistance(data::Resistance::Magic) << "  ("
                  << data::kResistanceImmune << " = immune)\n";
        return 0;
    }

    std::cout << monsters.size() << " monsters\n";
    for (const auto& m : monsters.entries()) {
        std::cout << "  " << m.id << "\t" << m.picture << "\t" << data::cp1252_to_utf8(m.name)
                  << "\tlvl " << m.level << "\thp " << m.hit_points << "\tac " << m.armor_class
                  << "\txp " << m.experience << "\n";
    }
    return 0;
}

int do_items(const std::filesystem::path& data_dir, const std::string& want) {
    data::ItemStatsTable items;
    if (data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: could not load ITEMS.TXT\n";
        return 1;
    }

    if (!want.empty()) {
        char* end = nullptr;
        const unsigned long id = std::strtoul(want.c_str(), &end, 10);
        if (end == want.c_str() || *end != '\0') {
            std::cerr << "error: item id must be a non-negative integer\n";
            return 2;
        }
        const auto* item = items.at(static_cast<std::size_t>(id));
        if (item == nullptr) {
            std::cerr << "error: no item id " << id << "\n";
            return 1;
        }
        std::cout << item->id << ": " << item->picture << " (" << data::cp1252_to_utf8(item->name)
                  << ")\n"
                  << "  value " << item->value << ", sprite " << item->sprite_index << ", shape "
                  << item->shape << "\n"
                  << "  equip " << item->equip_stat << ", skill " << item->skill_group
                  << ", material " << item->material << "\n";
        if (!item->unidentified_name.empty())
            std::cout << "  unidentified: " << data::cp1252_to_utf8(item->unidentified_name)
                      << "\n";
        return 0;
    }

    std::cout << items.size() << " item ids\n";
    for (const auto& item : items.entries()) {
        std::cout << "  " << item.id << "\t" << item.picture << "\t"
                  << data::cp1252_to_utf8(item.name) << "\tvalue " << item.value << "\tsprite "
                  << item.sprite_index << "\n";
    }
    return 0;
}

bool requested_id(const std::string& text, std::size_t& out) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

void print_weights(const auto& weights) {
    for (const int weight : weights) {
        std::cout << " " << weight;
    }
}

int do_random_items(const std::filesystem::path& data_dir, const std::string& want) {
    data::RandomItemTable random_items;
    if (data::load_random_items(data_dir, random_items) != data::GameDataError::None) {
        std::cerr << "error: could not load RNDITEMS.TXT\n";
        return 1;
    }

    if (!want.empty()) {
        std::size_t id = 0;
        if (!requested_id(want, id)) {
            std::cerr << "error: random item id must be a non-negative integer\n";
            return 2;
        }
        const auto* entry = random_items.at(id);
        if (entry == nullptr) {
            std::cerr << "error: no random item id " << id << "\n";
            return 1;
        }
        std::cout << entry->id << ": " << entry->picture << "\n  level weights:";
        print_weights(entry->weights);
        std::cout << "\n";
        return 0;
    }

    std::cout << random_items.size() << " random item ids\n";
    for (const auto& entry : random_items.entries()) {
        std::cout << "  " << entry.id << "\t" << entry.picture << "\tweights";
        print_weights(entry.weights);
        std::cout << "\n";
    }
    const auto& chances = random_items.bonus_chances();
    for (std::size_t level = 1; level <= data::kTreasureLevelCount; ++level) {
        std::cout << "  level " << level << " total " << random_items.total_weight(level)
                  << ", standard " << chances.standard[level - 1] << "%, special "
                  << chances.special[level - 1] << "%, weapon special "
                  << chances.weapon_special[level - 1] << "%\n";
    }
    return 0;
}

int do_standard_bonuses(const std::filesystem::path& data_dir, const std::string& want) {
    data::StandardBonusTable bonuses;
    if (data::load_standard_bonuses(data_dir, bonuses) != data::GameDataError::None) {
        std::cerr << "error: could not load STDITEMS.TXT\n";
        return 1;
    }

    if (!want.empty()) {
        std::size_t id = 0;
        if (!requested_id(want, id)) {
            std::cerr << "error: standard bonus id must be a non-negative integer\n";
            return 2;
        }
        const auto* entry = bonuses.at(id);
        if (entry == nullptr) {
            std::cerr << "error: no standard bonus id " << id << "\n";
            return 1;
        }
        std::cout << entry->id << ": " << data::cp1252_to_utf8(entry->stat) << " ("
                  << data::cp1252_to_utf8(entry->name_suffix) << ")\n  item-type weights:";
        print_weights(entry->chance_by_item_type);
        std::cout << "\n";
        return 0;
    }

    std::cout << bonuses.size() << " one-based standard bonuses\n";
    for (const auto& entry : bonuses.entries()) {
        std::cout << "  " << entry.id << "\t" << data::cp1252_to_utf8(entry.stat) << "\t"
                  << data::cp1252_to_utf8(entry.name_suffix) << "\n";
    }
    for (std::size_t level = 1; level <= data::kTreasureLevelCount; ++level) {
        const auto* range = bonuses.range(level);
        std::cout << "  level " << level << " strength " << range->minimum << ".." << range->maximum
                  << "\n";
    }
    std::cout << "  item-type totals:";
    for (std::size_t type = 0; type < data::kStandardBonusItemTypeCount; ++type) {
        std::cout << " " << bonuses.total_weight(type);
    }
    std::cout << "\n";
    return 0;
}

int do_special_bonuses(const std::filesystem::path& data_dir, const std::string& want) {
    data::SpecialBonusTable bonuses;
    if (data::load_special_bonuses(data_dir, bonuses) != data::GameDataError::None) {
        std::cerr << "error: could not load SPCITEMS.TXT\n";
        return 1;
    }

    if (!want.empty()) {
        std::size_t id = 0;
        if (!requested_id(want, id)) {
            std::cerr << "error: special bonus id must be a non-negative integer\n";
            return 2;
        }
        const auto* entry = bonuses.at(id);
        if (entry == nullptr) {
            std::cerr << "error: no special bonus id " << id << "\n";
            return 1;
        }
        std::cout << entry->id << ": " << data::cp1252_to_utf8(entry->name_affix) << "\n"
                  << "  " << data::cp1252_to_utf8(entry->effect) << "\n"
                  << "  value " << entry->value << ", treasure class "
                  << data::special_bonus_class_name(entry->treasure_class)
                  << "\n  item-type weights:";
        print_weights(entry->chance_by_item_type);
        std::cout << "\n";
        return 0;
    }

    std::cout << bonuses.size() << " one-based special bonuses\n";
    for (const auto& entry : bonuses.entries()) {
        std::cout << "  " << entry.id << "\t" << data::cp1252_to_utf8(entry.name_affix)
                  << "\tclass " << data::special_bonus_class_name(entry.treasure_class)
                  << "\tvalue " << entry.value << "\n";
    }
    for (std::size_t level = 3; level <= data::kTreasureLevelCount; ++level) {
        std::cout << "  level " << level << " eligible item-type totals:";
        for (std::size_t type = 0; type < data::kSpecialBonusItemTypeCount; ++type) {
            std::cout << " " << bonuses.total_weight(type, level);
        }
        std::cout << "\n";
    }
    return 0;
}

int do_generate_item(const std::filesystem::path& data_dir, const std::string& request) {
    const std::size_t first_separator = request.find(':');
    if (first_separator == std::string::npos) {
        std::cerr << "error: item generation expects LEVEL:SEED[:TYPE]\n";
        return 2;
    }
    const std::size_t second_separator = request.find(':', first_separator + 1);

    std::size_t level = 0;
    std::uint32_t seed = 0;
    std::uint32_t type_value = 0;
    const char* level_begin = request.data();
    const char* level_end = level_begin + first_separator;
    const char* seed_begin = level_end + 1;
    const char* request_end = request.data() + request.size();
    const char* seed_end =
        second_separator == std::string::npos ? request_end : request.data() + second_separator;
    const auto level_result = std::from_chars(level_begin, level_end, level);
    const auto seed_result = std::from_chars(seed_begin, seed_end, seed);
    std::from_chars_result type_result{request_end, std::errc{}};
    if (second_separator != std::string::npos) {
        type_result = std::from_chars(seed_end + 1, request_end, type_value);
    }
    if (level_result.ec != std::errc{} || level_result.ptr != level_end ||
        seed_result.ec != std::errc{} || seed_result.ptr != seed_end ||
        type_result.ec != std::errc{} || type_result.ptr != request_end || type_value > 255) {
        std::cerr << "error: item generation expects decimal LEVEL:SEED[:TYPE]\n";
        return 2;
    }
    const auto generation_type = static_cast<data::ItemGenerationType>(type_value);

    data::ItemStatsTable items;
    data::RandomItemTable random_items;
    data::StandardBonusTable standard_bonuses;
    data::SpecialBonusTable special_bonuses;
    if (data::load_item_stats(data_dir, items) != data::GameDataError::None ||
        data::load_random_items(data_dir, random_items) != data::GameDataError::None ||
        data::load_standard_bonuses(data_dir, standard_bonuses) != data::GameDataError::None ||
        data::load_special_bonuses(data_dir, special_bonuses) != data::GameDataError::None) {
        std::cerr << "error: could not load item-generation tables\n";
        return 1;
    }

    Mm6Random random(seed);
    data::ArtifactGenerationState artifacts;
    data::GeneratedItem generated;
    const auto error =
        data::generate_random_item(random_items, items, standard_bonuses, special_bonuses, level,
                                   generation_type, random, artifacts, generated);
    if (error != data::ItemGenerationError::None) {
        std::cerr << "error: item generation failed (" << static_cast<int>(error) << ")\n";
        return 1;
    }

    const auto* item = items.at(static_cast<std::size_t>(generated.item_id));
    std::cout << "level " << level << ", seed " << seed << ", type " << type_value << " ("
              << data::item_generation_type_name(generation_type) << ") -> item "
              << generated.item_id;
    if (item != nullptr) {
        std::cout << " (" << data::cp1252_to_utf8(item->name) << ", "
                  << data::item_equip_type_name(item->equip_type) << ")";
    }
    std::cout << "\n  standard " << generated.standard_bonus << " strength "
              << generated.standard_bonus_strength << ", special " << generated.special_bonus
              << ", charges " << generated.charges << ", identified "
              << (generated.identified ? "yes" : "no") << "\n"
              << "  final random state " << random.state() << "\n";
    return 0;
}

int do_spells(const std::filesystem::path& data_dir, const std::string& want) {
    data::SpellStatsTable spells;
    if (data::load_spell_stats(data_dir, spells) != data::GameDataError::None) {
        std::cerr << "error: could not load Spells.txt\n";
        return 1;
    }
    if (!want.empty()) {
        const auto* s = spells.find(want);
        if (s == nullptr) {
            std::cerr << "error: no spell named " << want << "\n";
            return 1;
        }
        std::cout << data::cp1252_to_utf8(s->name) << "  (" << data::school_name(s->school) << " "
                  << s->number << ", id " << s->id << ")\n"
                  << "  costs " << s->cost_normal << "/" << s->cost_expert << "/" << s->cost_master
                  << " at normal/expert/master";
        if (!s->element.empty() && s->element != "none") {
            std::cout << ", resisted as " << s->element;
        }
        std::cout << "\n  " << data::cp1252_to_utf8(s->description) << "\n";
        for (const auto& [label, text] :
             {std::pair{"normal", s->normal}, {"expert", s->expert}, {"master", s->master}}) {
            if (!text.empty()) {
                std::cout << "  " << label << ": " << data::cp1252_to_utf8(text) << "\n";
            }
        }
        return 0;
    }
    std::cout << spells.size() << " spells\n";
    for (const auto& s : spells.entries()) {
        std::cout << "  " << s.id << "\t" << data::school_name(s.school) << " " << s.number << "\t"
                  << data::cp1252_to_utf8(s.name) << "\tcost " << s.cost_normal << "/"
                  << s.cost_expert << "/" << s.cost_master << "\n";
    }
    return 0;
}

int do_buildings(const std::filesystem::path& data_dir, const std::string& want) {
    data::BuildingStatsTable buildings;
    if (data::load_building_stats(data_dir, buildings) != data::GameDataError::None) {
        std::cerr << "error: could not load 2DEvents.txt\n";
        return 1;
    }
    auto print = [](const data::BuildingStatsEntry& b) {
        std::cout << "  " << b.id << "\t" << b.map << "\t" << b.type << "\t"
                  << data::cp1252_to_utf8(b.name);
        if (!b.proprietor.empty() && b.proprietor != "0") {
            std::cout << "\t" << data::cp1252_to_utf8(b.proprietor);
            if (!b.title.empty() && b.title != "0") {
                std::cout << " the " << b.title;
            }
        }
        std::cout << "\t" << b.opens << ":00-" << b.closes << ":00\n";
    };

    if (!want.empty()) {
        // Accept either a map code or a Games.lod entry name.
        std::string code = data::map_code_of(want);
        if (code.empty()) {
            code = want;
        }
        const auto here = buildings.on_map(code);
        std::cout << here.size() << " establishments on " << code << "\n";
        for (const auto* b : here) {
            print(*b);
        }
        return 0;
    }

    std::size_t placed = 0;
    for (const auto& b : buildings.entries()) {
        if (!b.map_code().empty()) {
            ++placed;
        }
    }
    std::cout << buildings.size() << " establishments, " << placed << " on a single named map\n";
    for (const auto& b : buildings.entries()) {
        print(b);
    }
    return 0;
}

int do_npcs(const std::filesystem::path& data_dir, const std::string& want) {
    data::NpcTable npcs;
    data::NpcProfessionTable professions;
    data::BuildingStatsTable buildings;
    if (data::load_npcs(data_dir, npcs) != data::GameDataError::None) {
        std::cerr << "error: could not load NPCdata.txt\n";
        return 1;
    }
    (void)data::load_npc_professions(data_dir, professions);
    (void)data::load_building_stats(data_dir, buildings);

    auto building_of = [&](int id) -> const data::BuildingStatsEntry* {
        for (const auto& b : buildings.entries()) {
            if (b.id == id) {
                return &b;
            }
        }
        return nullptr;
    };

    std::size_t placed = 0;
    std::size_t placed_ok = 0;
    std::size_t employed = 0;
    std::size_t employed_ok = 0;
    for (const auto& n : npcs.entries()) {
        if (n.placed()) {
            ++placed;
            placed_ok += building_of(n.building_id) != nullptr;
        }
        if (n.profession_id != 0) {
            ++employed;
            employed_ok += professions.at(n.profession_id) != nullptr;
        }
    }

    if (want.empty()) {
        std::cout << npcs.size() << " people; " << placed_ok << "/" << placed
                  << " stand in an establishment that exists, " << employed_ok << "/" << employed
                  << " hold a profession that exists\n";
    }

    for (const auto& n : npcs.entries()) {
        if (!want.empty() && n.name.find(want) == std::string::npos) {
            continue;
        }
        std::cout << "  " << n.id << "\t" << data::cp1252_to_utf8(n.name);
        if (const auto* p = professions.at(n.profession_id); p != nullptr) {
            std::cout << ", " << p->name;
        }
        if (const auto* b = building_of(n.building_id); b != nullptr) {
            std::cout << "\tat " << data::cp1252_to_utf8(b->name) << " (" << b->map << ")";
        }
        if (n.can_join) {
            std::cout << "\tjoins";
        }
        if (n.has_news) {
            std::cout << "\thas news";
        }
        std::cout << "\n";
    }
    return 0;
}

int do_dialogue(const std::filesystem::path& data_dir, const std::string& want) {
    data::NpcDialogueTable dialogue;
    if (data::load_npc_dialogue(data_dir, dialogue) != data::GameDataError::None) {
        std::cerr << "error: could not load the dialogue tables\n";
        return 1;
    }
    if (!want.empty()) {
        const int id = std::atoi(want.c_str());
        const auto* e = dialogue.at(id);
        if (e == nullptr) {
            std::cerr << "error: no dialogue with id " << id << "\n";
            return 1;
        }
        std::cout << e->id << "  " << data::cp1252_to_utf8(e->topic) << "\n"
                  << "  " << data::cp1252_to_utf8(e->text) << "\n";
        return 0;
    }

    // How many NPC event references land on a topic, and how many of those
    // topics actually have words.
    data::NpcTable npcs;
    std::size_t referenced = 0;
    std::size_t resolved = 0;
    std::size_t named = 0;
    std::size_t spoken = 0;
    if (data::load_npcs(data_dir, npcs) == data::GameDataError::None) {
        for (const auto& n : npcs.entries()) {
            for (const int id : n.events) {
                if (id <= 0) {
                    continue;
                }
                ++referenced;
                if (const auto* e = dialogue.at(id); e != nullptr) {
                    ++resolved;
                    spoken += !e->text.empty();
                }
            }
        }
    }
    std::size_t wordless = 0;
    for (const auto& e : dialogue.entries()) {
        wordless += e.text.empty();
    }
    std::cout << dialogue.size() << " topics, " << wordless << " with no words\n";
    std::cout << resolved << "/" << referenced << " NPC references resolve, " << spoken
              << " of them have words\n";
    for (const auto& e : dialogue.entries()) {
        std::cout << "  " << e.id << "\t" << data::cp1252_to_utf8(e.topic) << "\n";
    }
    return 0;
}

int do_news(const std::filesystem::path& data_dir) {
    data::NpcNewsTable news;
    if (data::load_npc_news(data_dir, news) != data::GameDataError::None) {
        std::cerr << "error: could not load NPCNews.txt\n";
        return 1;
    }
    std::cout << news.size() << " regional rumours\n";
    for (const auto& n : news.entries()) {
        std::cout << "  " << n.id << "\t[" << n.map_value << "]\t" << data::cp1252_to_utf8(n.topic)
                  << "\t" << data::cp1252_to_utf8(n.text).substr(0, 70) << "\n";
    }
    return 0;
}

// The three bit-keyed journal tables print the same way.
int do_journal(const std::string& label, const data::JournalTable& table, const std::string& want) {
    if (!want.empty()) {
        const int bit = std::atoi(want.c_str());
        const auto* e = table.at(bit);
        if (e == nullptr) {
            std::cerr << "error: " << label << " has no bit " << bit << "\n";
            return 1;
        }
        std::cout << e->bit << "  " << data::cp1252_to_utf8(e->text) << "\n";
        if (!e->category.empty()) {
            std::cout << "  category: " << data::cp1252_to_utf8(e->category) << "\n";
        }
        if (!e->alternate.empty()) {
            std::cout << "  older wording: " << data::cp1252_to_utf8(e->alternate) << "\n";
        }
        if (!e->notes.empty()) {
            std::cout << "  designers' note: " << data::cp1252_to_utf8(e->notes) << "\n";
        }
        return 0;
    }

    std::cout << table.size() << " " << label << ", " << table.written() << " with text\n";
    for (const auto& e : table.entries()) {
        if (!e.has_text()) {
            continue;
        }
        std::cout << "  " << e.bit << "\t";
        if (!e.category.empty()) {
            std::cout << "[" << data::cp1252_to_utf8(e.category) << "]\t";
        }
        std::cout << data::cp1252_to_utf8(e.text).substr(0, 90) << "\n";
    }
    return 0;
}

// Research mode: does every damage cell in either table parse?
int do_treasure(const std::filesystem::path& data_dir) {
    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: could not load MONSTERS.TXT\n";
        return 1;
    }
    std::size_t coded = 0;
    std::size_t parsed = 0;
    std::map<std::string, std::size_t> kinds;
    for (const auto& m : monsters.entries()) {
        if (m.treasure.empty() || m.treasure == "0") {
            continue;
        }
        ++coded;
        const data::Treasure drop = data::parse_treasure(m.treasure);
        parsed += drop.empty() ? 0 : 1;
        if (drop.empty()) {
            std::cout << "  unparsed: " << m.name << " \"" << m.treasure << "\"\n";
        }
        if (drop.item_level > 0) {
            ++kinds[drop.item_kind.empty() ? "(any)" : drop.item_kind];
        }
    }
    std::cout << parsed << "/" << coded << " treasure codes parse\n";
    for (const auto& [kind, count] : kinds) {
        std::cout << "  item kind " << kind << ": " << count << " (generator takes "
                  << data::item_generation_type_name(data::treasure_item_type(kind)) << ")\n";
    }
    return 0;
}

int do_dice(const std::filesystem::path& data_dir) {
    data::TextTable text;
    data::MonsterStatsTable monsters;
    data::ItemStatsTable items;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: could not load the tables\n";
        return 1;
    }

    std::size_t attacks = 0;
    std::size_t parsed = 0;
    for (const auto& m : monsters.entries()) {
        for (const auto& a : m.attacks) {
            if (a.damage.empty() || a.damage == "0") {
                continue;
            }
            ++attacks;
            const data::Dice dice = data::parse_dice(a.damage);
            parsed += dice.empty() ? 0 : 1;
            if (dice.empty()) {
                std::cout << "  unparsed: " << m.name << " \"" << a.damage << "\"\n";
            }
        }
    }
    std::cout << parsed << "/" << attacks << " monster attack damages parse\n";

    // Only the things you hit with carry dice; armour's modifier is a flat
    // number and a wand's is charges.
    std::size_t weapons = 0;
    std::size_t weapon_dice = 0;
    for (const auto& item : items.entries()) {
        const bool hits = item.equip_type == data::ItemEquipType::Weapon ||
                          item.equip_type == data::ItemEquipType::TwoHandedWeapon ||
                          item.equip_type == data::ItemEquipType::Missile;
        if (!hits) {
            continue;
        }
        ++weapons;
        weapon_dice += data::parse_dice(item.modifier_1).empty() ? 0 : 1;
        if (data::parse_dice(item.modifier_1).empty()) {
            std::cout << "  no dice: " << item.name << " \"" << item.modifier_1 << "\"\n";
        }
    }
    std::cout << weapon_dice << "/" << weapons << " weapons carry damage dice\n";
    return 0;
}

int do_encounters(const std::filesystem::path& data_dir) {
    data::MapStatsTable maps;
    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (data::load_map_stats(data_dir, maps) != data::GameDataError::None ||
        data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: could not load the tables\n";
        return 1;
    }

    // Each slot names a monster; the spawn points reference the slot. Whether
    // the name is one MONSTERS.TXT has is the join worth measuring.
    std::size_t filled = 0;
    std::size_t resolved = 0;
    std::size_t named = 0;
    for (const auto& m : maps.entries()) {
        for (const auto& e : m.monsters) {
            if (e.empty()) {
                continue;
            }
            ++filled;
            bool found = false;
            for (std::size_t i = 0; i < monsters.entries().size() && !found; ++i) {
                found = monsters.entries()[i].name == e.monster;
            }
            // The picture names a triple: "Rat" is RatA, RatB and RatC, and
            // the slot's own name is the first of the three.
            // Uniques have no triple: the table prefixes theirs with a "z".
            const auto* row = monsters.find(e.picture + "A");
            if (row == nullptr) {
                row = monsters.find(e.picture);
            }
            if (row == nullptr) {
                row = monsters.find("z" + e.picture);
            }
            resolved += row != nullptr ? 1 : 0;
            named += row != nullptr && row->name == e.monster ? 1 : 0;
            if (row == nullptr) {
                std::cout << "  unresolved: " << m.file_name << " \"" << e.monster << "\" ("
                          << e.picture << ")\n";
            }
        }
    }
    std::cout << resolved << "/" << filled
              << " filled encounter slots resolve to a MONSTERS.TXT row through their picture; "
              << named << " of those also match its name\n";
    return 0;
}

int do_profession_text(const std::filesystem::path& data_dir, const std::string& want) {
    data::ProfessionTextTable said;
    data::NpcProfessionTable professions;
    if (data::load_profession_text(data_dir, said) != data::GameDataError::None) {
        std::cerr << "error: could not load PROFTEXT.txt\n";
        return 1;
    }
    (void)data::load_npc_professions(data_dir, professions);

    if (!want.empty()) {
        const auto* row = said.at(std::atoi(want.c_str()));
        if (row == nullptr) {
            std::cerr << "error: no profession with id " << want << "\n";
            return 1;
        }
        std::cout << row->id << "  " << row->name << "\n";
        static constexpr std::array<const char*, data::kProfessionDayCount> kDays{
            "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        for (std::size_t d = 0; d < data::kProfessionDayCount; ++d) {
            std::cout << "  " << kDays[d] << ": " << data::cp1252_to_utf8(row->days[d].topic)
                      << " \u2014 " << data::cp1252_to_utf8(row->days[d].text) << "\n";
        }
        return 0;
    }

    // Every profession that can be hired should have something to say.
    std::size_t matched = 0;
    std::size_t named = 0;
    for (const auto& p : professions.entries()) {
        const auto* row = said.at(p.id);
        matched += row != nullptr ? 1 : 0;
        named += row != nullptr && row->name == p.name ? 1 : 0;
    }
    std::size_t days = 0;
    for (const auto& row : said.entries()) {
        for (const auto& day : row.days) {
            days += day.empty() ? 0 : 1;
        }
    }
    std::cout << said.size() << " professions have something to say; " << matched << "/"
              << professions.size() << " of npcprof.txt's resolve here, " << named
              << " with the same name\n";
    std::cout << days << "/" << said.size() * data::kProfessionDayCount
              << " profession-days are filled in\n";
    return 0;
}

int do_personalities(const std::filesystem::path& data_dir) {
    data::NpcPersonalityTable personalities;
    if (data::load_npc_personalities(data_dir, personalities) != data::GameDataError::None) {
        std::cerr << "error: could not load npcbtb.txt\n";
        return 1;
    }
    data::NpcProfessionTable professions;
    (void)data::load_npc_professions(data_dir, professions);

    // Every personality a profession names should be one this table describes.
    std::set<std::string> named;
    for (const auto& p : professions.entries()) {
        if (!p.personality.empty()) {
            named.insert(p.personality);
        }
    }
    std::size_t matched = 0;
    for (const auto& n : named) {
        matched += personalities.find(n) != nullptr;
    }
    std::cout << personalities.size() << " personalities; " << matched << "/" << named.size()
              << " of those the professions name are described here\n";

    // The matrix and the messages say the same thing twice: a personality has
    // an acceptance line exactly where the matrix allows the approach, and a
    // refusal line exactly where it does not.
    struct Pair {
        data::NpcApproach approach;
        const char* accepts;
        const char* refuses;
    };
    static constexpr std::array<Pair, 3> kPairs{{
        {data::NpcApproach::Beg, "I accept your beg", "I don't like begging"},
        {data::NpcApproach::Bribe, "Thanks for the bribe", "I don't like bribes"},
        {data::NpcApproach::Threat, "I accept your threat", "I don't like threats"},
    }};
    const auto numbered = [&](const char* note) {
        for (int n = 1; n < 64; ++n) {
            if (personalities.note(n) == note) {
                return n;
            }
        }
        return 0;
    };
    std::size_t agree = 0;
    std::size_t pairs = 0;
    for (const auto& pair : kPairs) {
        const int accepts = numbered(pair.accepts);
        const int refuses = numbered(pair.refuses);
        for (const auto& p : personalities.entries()) {
            ++pairs;
            const bool allowed = p.allows_approach(pair.approach);
            agree +=
                !p.message(accepts).empty() == allowed && p.message(refuses).empty() == allowed;
        }
    }
    std::cout << agree << "/" << pairs
              << " personality and approach pairs agree with their messages\n";

    for (const auto& p : personalities.entries()) {
        std::cout << "  " << p.name << "\t";
        std::cout << (p.allows_approach(data::NpcApproach::Beg) ? "beg " : "    ");
        std::cout << (p.allows_approach(data::NpcApproach::Bribe) ? "bribe " : "      ");
        std::cout << (p.allows_approach(data::NpcApproach::Threat) ? "threat" : "      ");
        std::cout << "\t" << data::cp1252_to_utf8(std::string(p.message(1))).substr(0, 60) << "\n";
    }
    return 0;
}

int do_strings(const std::filesystem::path& data_dir, const std::string& want) {
    data::InterfaceStrings strings;
    if (data::load_interface_strings(data_dir, strings) != data::GameDataError::None) {
        std::cerr << "error: could not load GLOBAL.TXT\n";
        return 1;
    }
    if (!want.empty()) {
        const int id = std::atoi(want.c_str());
        const std::string_view text = strings.at(id);
        if (text.empty()) {
            std::cerr << "error: no string with id " << id << "\n";
            return 1;
        }
        std::cout << id << "  " << data::cp1252_to_utf8(std::string(text)) << "\n";
        return 0;
    }
    std::cout << strings.size() << " interface strings\n";
    for (int id = 0; id < static_cast<int>(strings.size()); ++id) {
        const std::string_view text = strings.at(id);
        if (!text.empty()) {
            std::cout << "  " << id << "\t" << data::cp1252_to_utf8(std::string(text)) << "\n";
        }
    }
    return 0;
}

int do_professions(const std::filesystem::path& data_dir) {
    data::NpcProfessionTable professions;
    if (data::load_npc_professions(data_dir, professions) != data::GameDataError::None) {
        std::cerr << "error: could not load npcprof.txt\n";
        return 1;
    }
    std::cout << professions.size() << " professions\n";
    for (const auto& p : professions.entries()) {
        std::cout << "  " << p.id << "\t" << p.name << "\t" << p.hire_cost << " a week\t"
                  << p.personality;
        if (!p.party_benefit.empty()) {
            std::cout << "\t" << data::cp1252_to_utf8(p.party_benefit);
        }
        std::cout << "\n";
    }
    return 0;
}

int do_descriptions(const std::filesystem::path& data_dir, const char* entry) {
    data::DescriptionTable table;
    if (data::load_descriptions(data_dir, entry, table) != data::GameDataError::None) {
        std::cerr << "error: could not load " << entry << "\n";
        return 1;
    }
    std::cout << table.size() << " entries in " << entry << "\n";
    for (const auto& e : table.entries()) {
        std::cout << "  " << data::cp1252_to_utf8(e.name) << "\n";
        for (const auto& text : e.text) {
            std::cout << "      " << data::cp1252_to_utf8(text) << "\n";
        }
    }
    return 0;
}

// The join this slice makes possible: MONSTERS.TXT's "Picture" column against
// the DMONLIST.BIN names an actor record's monster id indexes.
int do_check(const std::filesystem::path& data_dir) {
    data::TextTable table;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", table) != data::GameDataError::None) {
        std::cerr << "error: could not load MONSTERS.TXT\n";
        return 1;
    }
    data::MonsterStatsTable monsters;
    if (data::MonsterStatsTable::parse(table, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: MONSTERS.TXT has an unexpected header\n";
        return 1;
    }

    lod::LodArchive archive;
    std::span<const std::byte> raw;
    world::MonsterList list;
    if (lod::LodArchive::open(data_dir / "icons.lod", archive) != lod::LodError::None ||
        archive.payload("DMONLIST.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::MonsterList::parse(raw, list) != world::MonsterListError::None) {
        std::cerr << "error: could not load DMONLIST.BIN\n";
        return 1;
    }

    std::cout << "MONSTERS.TXT: " << monsters.size() << " rows\n"
              << "DMONLIST.BIN: " << list.size() << " records\n";

    // The text table numbers monsters from 1; the binary table is indexed from
    // 0. If that is the whole relationship, row id N names record N-1.
    std::size_t exact = 0;
    std::size_t normalized = 0;
    std::size_t mismatched = 0;
    for (const auto& m : monsters.entries()) {
        const auto* record = list.at(static_cast<std::size_t>(m.id) - 1);
        if (record == nullptr) {
            ++mismatched;
            continue;
        }
        if (record->name == m.picture) {
            ++exact;
        } else if (data::normalize_picture(record->name) == data::normalize_picture(m.picture)) {
            ++normalized;
            std::cout << "  id " << m.id << ": text \"" << m.picture << "\" vs binary \""
                      << record->name << "\"  (differs only in case or spacing)\n";
        } else {
            ++mismatched;
            std::cout << "  id " << m.id << ": text \"" << m.picture << "\" vs binary \""
                      << record->name << "\"\n";
        }
    }
    std::cout << exact << " names match exactly at id-1, " << normalized
              << " after normalizing case and spaces, " << mismatched << " not at all\n";
    return mismatched == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string command;
    std::string argument;
    std::size_t limit = 20;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--rows" && i + 1 < argc) {
            limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (command.empty()) {
            command = a;
        } else if (argument.empty()) {
            argument = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (command.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    const auto install = platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << platform::kInstallEnvVar << "\n";
        return 1;
    }
    const std::filesystem::path data_dir = *install / "data";

    if (command == "--list")
        return do_list(data_dir);
    if (command == "--maps")
        return do_maps(data_dir);
    if (command == "--monsters")
        return do_monsters(data_dir, argument);
    if (command == "--items")
        return do_items(data_dir, argument);
    if (command == "--random-items")
        return do_random_items(data_dir, argument);
    if (command == "--standard-bonuses")
        return do_standard_bonuses(data_dir, argument);
    if (command == "--special-bonuses")
        return do_special_bonuses(data_dir, argument);
    if (command == "--generate-item")
        return do_generate_item(data_dir, argument);
    if (command == "--spells")
        return do_spells(data_dir, argument);
    if (command == "--buildings")
        return do_buildings(data_dir, argument);
    if (command == "--npcs")
        return do_npcs(data_dir, argument);
    if (command == "--dialogue")
        return do_dialogue(data_dir, argument);
    if (command == "--news")
        return do_news(data_dir);
    if (command == "--quests" || command == "--awards" || command == "--autonotes") {
        data::JournalTable table;
        const data::GameDataError e = command == "--quests" ? data::load_quests(data_dir, table)
                                      : command == "--awards"
                                          ? data::load_awards(data_dir, table)
                                          : data::load_autonotes(data_dir, table);
        if (e != data::GameDataError::None) {
            std::cerr << "error: could not load " << command.substr(2) << "\n";
            return 1;
        }
        return do_journal(command.substr(2), table, argument);
    }
    if (command == "--spell-effects") {
        data::SpellStatsTable spells;
        if (data::load_spell_stats(data_dir, spells) != data::GameDataError::None) {
            std::cerr << "error: could not read Spells.txt\n";
            return 1;
        }
        std::size_t heals = 0, damages = 0, scaled = 0, silent = 0;
        for (const auto& spell : spells.entries()) {
            const data::SpellEffect effect = data::parse_spell_effect(spell, 0);
            if (effect.empty()) {
                ++silent;
                continue;
            }
            std::cout << "  " << spell.id << "\t" << data::cp1252_to_utf8(spell.name);
            if (!effect.heal.empty()) {
                ++heals;
                std::cout << "\theal " << effect.heal.low << "-" << effect.heal.high;
            }
            if (!effect.damage.empty()) {
                ++damages;
                std::cout << "\tdamage " << effect.damage.low << "-" << effect.damage.high;
            }
            if (!effect.damage_per_skill.empty()) {
                ++scaled;
                std::cout << "\tdamage/skill " << effect.damage_per_skill.low << "-"
                          << effect.damage_per_skill.high;
            }
            std::cout << "\n";
        }
        std::cout << spells.size() << " spells; " << heals << " heal, " << damages
                  << " state flat damage, " << scaled << " scale with skill, " << silent
                  << " state no number this parser reads\n";
        return 0;
    }
    if (command == "--use-items") {
        data::UseItemTable use_items;
        if (data::load_use_items(data_dir, use_items) != data::GameDataError::None) {
            std::cerr << "error: could not read USEITEMS.TXT\n";
            return 1;
        }
        std::size_t cures = 0;
        std::size_t transforms = 0;
        std::size_t mixes = 0;
        std::size_t explosions = 0;
        for (const auto& entry : use_items.entries()) {
            cures += entry.cure_hit_points > 0 || entry.cure_spell_points > 0 ? 1 : 0;
            transforms += entry.becomes_item > 0 ? 1 : 0;
            for (const auto& mix : entry.mixes) {
                mixes += mix.kind == data::MixKind::Item ? 1 : 0;
                explosions += mix.kind == data::MixKind::Explosion ? 1 : 0;
            }
            std::cout << "  " << entry.id << "\t" << data::cp1252_to_utf8(entry.name) << "\t"
                      << data::cp1252_to_utf8(entry.effect);
            if (entry.cure_hit_points > 0)
                std::cout << "\t[+" << entry.cure_hit_points << " hp]";
            if (entry.cure_spell_points > 0)
                std::cout << "\t[+" << entry.cure_spell_points << " sp]";
            std::cout << "\n";
        }
        std::cout << use_items.size() << " usable items; " << cures << " cure, " << transforms
                  << " become another item; " << mixes << " mixes yield a potion and "
                  << explosions << " blow up\n";
        return 0;
    }
    if (command == "--treasure")
        return do_treasure(data_dir);
    if (command == "--dice")
        return do_dice(data_dir);
    if (command == "--encounters")
        return do_encounters(data_dir);
    if (command == "--proftext")
        return do_profession_text(data_dir, argument);
    if (command == "--personalities")
        return do_personalities(data_dir);
    if (command == "--strings")
        return do_strings(data_dir, argument);
    if (command == "--professions")
        return do_professions(data_dir);
    if (command == "--classes")
        return do_descriptions(data_dir, "Class.txt");
    if (command == "--stats")
        return do_descriptions(data_dir, "stats.txt");
    if (command == "--skills")
        return do_descriptions(data_dir, "SKILLDES.TXT");
    if (command == "--check")
        return do_check(data_dir);
    if (command.rfind("--", 0) == 0) {
        print_usage(argv[0]);
        return 2;
    }
    return do_dump(data_dir, command, limit);
}
