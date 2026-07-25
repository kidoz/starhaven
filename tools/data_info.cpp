#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/data/game_data.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/text_table.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/monster_list.hpp"

namespace {

using namespace starhaven;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--list | --maps | --monsters [name] | --check"
              << " | <Table.txt>>\n"
              << "\n"
              << "Reads the tab-separated design tables shipped inside your own\n"
              << "legal installation's icons.lod.\n"
              << "\n"
              << "  --list             list the tables and their sizes\n"
              << "  --maps             MapStats.txt as typed rows\n"
              << "  --monsters [name]  MONSTERS.TXT as typed rows, or one monster\n"
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
    if (command == "--check")
        return do_check(data_dir);
    if (command.rfind("--", 0) == 0) {
        print_usage(argv[0]);
        return 2;
    }
    return do_dump(data_dir, command, limit);
}
