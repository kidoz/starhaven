#ifndef STARHAVEN_CORE_DATA_BUILDING_STATS_HPP
#define STARHAVEN_CORE_DATA_BUILDING_STATS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// One row of `2DEvents.txt`: a shop, temple, tavern, guild or training hall.
struct BuildingStatsEntry {
    int id = 0;        // 1-based row number
    int type_id = 0;   // the second number, which restarts within each type
    std::string type;  // "Weapon Shop", "Temple", "Tavern", ...
    std::string map;   // the map code as written, e.g. "E3" or "D2,C3,B1"
    std::string name;  // "The Knife Shoppe"
    std::string proprietor;
    std::string title;  // "Blacksmith", "Barkeep", ...

    // What the shop charges over an item's value: 1.5 or 2 on the shipped
    // rows. The column is headed "Val".
    float price_factor = 1.0f;

    // What it stocks or offers, at the three quality levels the table gives.
    std::string stock_a;
    std::string stock_b;
    std::string stock_c;

    int opens = 0;  // hour of day
    int closes = 0;

    std::string notes;

    // The single map this belongs to, or empty when the cell names several or
    // is not a map code at all. See docs/formats/text-tables.md.
    [[nodiscard]] std::string_view map_code() const noexcept;
};

enum class BuildingStatsError : std::uint8_t {
    None,
    // No row carries the expected "Type" / "Map" header.
    NoHeader,
};

// `2DEvents.txt`, parsed into rows.
class BuildingStatsTable {
public:
    BuildingStatsTable() = default;

    [[nodiscard]] static BuildingStatsError parse(const TextTable& table, BuildingStatsTable& out);

    [[nodiscard]] const std::vector<BuildingStatsEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Every building whose map cell is exactly this code, ignoring case.
    // Rows naming several maps are not returned by any of them: which one they
    // belong to is not established.
    [[nodiscard]] std::vector<const BuildingStatsEntry*> on_map(std::string_view code) const;

private:
    std::vector<BuildingStatsEntry> entries_;
};

// The map code for a Games.lod entry name: "OutE3.Odm" gives "E3". Empty for
// an indoor map, which the table never references.
[[nodiscard]] std::string map_code_of(std::string_view file_name);

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_BUILDING_STATS_HPP
