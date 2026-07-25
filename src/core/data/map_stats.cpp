#include "core/data/map_stats.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

// Column indices, taken from the header row rather than assumed: `parse`
// locates the header first and refuses a table whose columns do not match.
constexpr std::size_t kColId = 0;
constexpr std::size_t kColName = 1;
constexpr std::size_t kColFile = 2;
constexpr std::size_t kColResetCount = 3;
constexpr std::size_t kColFirstVisitDay = 4;
constexpr std::size_t kColRefillDays = 5;
constexpr std::size_t kColLock = 6;
constexpr std::size_t kColTrap = 7;
constexpr std::size_t kColTreasure = 8;
constexpr std::size_t kColEncounterPercent = 9;
constexpr std::size_t kColGroupPercent = 10;  // three columns
constexpr std::size_t kColEncounters = 13;    // three slots of four columns
constexpr std::size_t kEncounterStride = 4;
constexpr std::size_t kColMusicTrack = 25;
constexpr std::size_t kColDesigner = 26;

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

}  // namespace

MapStatsError MapStatsTable::parse(const TextTable& table, MapStatsTable& out) {
    out.entries_.clear();

    // The export carries three rows of merged spreadsheet headings before the
    // data, so find the real header rather than counting rows.
    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, kColName)), "Name") &&
            iequals(trim(table.cell(r, kColFile)), "File name")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return MapStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        // The table ends in a block of blank rows. A row without both an id and
        // a file name is not a map.
        const int id = table.cell_int(r, kColId, -1);
        std::string file = cell_text(table, r, kColFile);
        if (id < 0 || file.empty()) {
            continue;
        }

        MapStatsEntry e;
        e.id = id;
        e.name = cell_text(table, r, kColName);
        e.file_name = std::move(file);
        e.reset_count = table.cell_int(r, kColResetCount);
        e.first_visit_day = table.cell_int(r, kColFirstVisitDay);
        e.refill_days = table.cell_int(r, kColRefillDays);
        e.lock_difficulty = table.cell_int(r, kColLock);
        e.trap_difficulty = table.cell_int(r, kColTrap);
        e.treasure_level = table.cell_int(r, kColTreasure);
        e.encounter_percent = table.cell_int(r, kColEncounterPercent);
        for (std::size_t i = 0; i < e.group_percent.size(); ++i) {
            e.group_percent[i] = table.cell_int(r, kColGroupPercent + i);
        }
        for (std::size_t i = 0; i < e.monsters.size(); ++i) {
            const std::size_t base = kColEncounters + i * kEncounterStride;
            MapEncounter& m = e.monsters[i];
            m.picture = cell_text(table, r, base);
            m.monster = cell_text(table, r, base + 1);
            m.difficulty = table.cell_int(r, base + 2);
            m.count = cell_text(table, r, base + 3);
        }
        e.music_track = table.cell_int(r, kColMusicTrack);
        e.designer = cell_text(table, r, kColDesigner);
        out.entries_.push_back(std::move(e));
    }
    return MapStatsError::None;
}

const MapStatsEntry* MapStatsTable::find(std::string_view file_name) const noexcept {
    for (const auto& e : entries_) {
        if (iequals(e.file_name, file_name))
            return &e;
    }
    return nullptr;
}

}  // namespace starhaven::data
