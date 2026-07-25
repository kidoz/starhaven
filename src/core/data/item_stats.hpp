#ifndef STARHAVEN_CORE_DATA_ITEM_STATS_HPP
#define STARHAVEN_CORE_DATA_ITEM_STATS_HPP

#include <string>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// One row of `ITEMS.TXT`. Names follow the shipped column headings where the
// engine meaning has not yet been independently established.
struct ItemStatsEntry {
    int id = 0;  // direct, zero-based item id
    std::string picture;
    std::string name;
    int value = 0;
    std::string equip_stat;
    std::string skill_group;
    std::string modifier_1;
    int modifier_2 = 0;
    std::string material;  // numeric codes plus the literals "Artifact" and "Relic"
    int id_rep_st = 0;
    std::string unidentified_name;
    int sprite_index = 0;
    int shape = 0;
    int equip_x = 0;
    int equip_y = 0;
    std::string notes;
};

enum class ItemStatsError {
    None,
    // No row carries the expected "Item #" / "Pic File" header.
    NoHeader,
    // Item ids are not the contiguous zero-based sequence the binary records
    // index directly.
    BadId,
};

// `ITEMS.TXT`, parsed into direct-id-addressable rows.
class ItemStatsTable {
public:
    ItemStatsTable() = default;

    [[nodiscard]] static ItemStatsError parse(const TextTable& table, ItemStatsTable& out);

    [[nodiscard]] const std::vector<ItemStatsEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Look an item up by the direct zero-based id stored in item instances.
    [[nodiscard]] const ItemStatsEntry* at(std::size_t id) const noexcept;

private:
    std::vector<ItemStatsEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_ITEM_STATS_HPP
