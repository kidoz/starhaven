#ifndef STARHAVEN_CORE_DATA_MAP_STATS_HPP
#define STARHAVEN_CORE_DATA_MAP_STATS_HPP

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// One of a map's three random-encounter slots.
struct MapEncounter {
    std::string picture;  // a DMONLIST.BIN / MONSTERS.TXT picture name
    std::string monster;  // the display name, e.g. "Devil Spawn"
    int difficulty = 0;   // the table's "Dif 1-5" column
    std::string count;    // an appearance range as written, e.g. "2-4"

    [[nodiscard]] bool empty() const noexcept { return picture.empty() || picture == "0"; }
};

// One row of `MapStats.txt`: everything the designers recorded about a map.
struct MapStatsEntry {
    int id = 0;             // 1-based row number
    std::string name;       // display name, e.g. "Sweet Water"
    std::string file_name;  // the Games.lod entry, e.g. "OutA1.Odm"

    int reset_count = 0;      // "Reset #"
    int first_visit_day = 0;  // "First Visit Day"
    int refill_days = 0;      // "Refil Days"
    int lock_difficulty = 0;  // "Lock 0-10"
    int trap_difficulty = 0;  // "Trap 0-10"
    int treasure_level = 0;   // "Tres 0-6"

    int encounter_percent = 0;             // "Enc %"
    std::array<int, 3> group_percent{};    // "M1 %", "M2 %", "M3 %"
    std::array<MapEncounter, 3> monsters;  // the three encounter slots

    // Index of the music track for this map — the `N` in `Sounds/N.mp3`. The
    // column is headed "Redbook Track" after the CD audio the original used.
    int music_track = 0;

    std::string designer;

    // Unfinished maps ship with the placeholder name the designers left.
    [[nodiscard]] bool placeholder() const noexcept { return name == "pending"; }
};

enum class MapStatsError {
    None,
    // No row carries the expected "Name" / "File name" header.
    NoHeader,
};

// `MapStats.txt`, parsed into rows.
class MapStatsTable {
public:
    MapStatsTable() = default;

    [[nodiscard]] static MapStatsError parse(const TextTable& table, MapStatsTable& out);

    [[nodiscard]] const std::vector<MapStatsEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Look a map up by its Games.lod entry name, case-insensitively: the table
    // writes "OutA1.Odm" where the archive holds "outa1.odm". Returns nullptr
    // when the name is not listed.
    [[nodiscard]] const MapStatsEntry* find(std::string_view file_name) const noexcept;

private:
    std::vector<MapStatsEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_MAP_STATS_HPP
