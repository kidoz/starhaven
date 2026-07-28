#ifndef STARHAVEN_CORE_DATA_USE_ITEMS_HPP
#define STARHAVEN_CORE_DATA_USE_ITEMS_HPP

// `USEITEMS.TXT`: what using a thing does.
//
// The rows are the herbs and potions, items 160..188: each carries its
// effect in the designers' prose — `"Cure 10 Hit points"`, `"Set Haste to
// 6 Hrs"` — what becomes of the item (`"remove Item"`, or `"Change Item to
// 163"`, the empty bottle), and a full mixing matrix against every other
// potion: the yield of pouring one into another, or an explosion graded
// E1..E4, whose meanings the sheet's own header writes out. Scrolls and
// books are covered by their `ITEMS.TXT` rows instead: a spell scroll's
// first modifier is `S47`-style, the spell it casts. See
// docs/formats/text-tables.md.

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// What mixing two potions yields.
enum class MixKind : std::uint8_t {
    None,       // "no": they do not combine
    Item,       // a new potion
    Explosion,  // E1..E4
};

struct MixResult {
    MixKind kind = MixKind::None;
    int item_id = 0;         // when a new potion
    int explosion_grade = 0;  // 1..4 when it blows up
};

// One usable item's row.
struct UseItemEntry {
    int id = 0;
    std::string name;
    std::string kind;    // "Herb", "Red Potion", ...
    std::string effect;  // the designers' own words

    // The effects this engine applies directly, parsed from the prose;
    // zero when the effect is something else.
    int cure_hit_points = 0;
    int cure_spell_points = 0;
    int temp_stats = 0;        // "Set Temp 7 Stats to 10"
    int temp_armor = 0;        // "Set Temp AC to 10"
    int temp_resistances = 0;  // "Set Temp [4] Resistances to 10"

    // "Set Haste to 6 Hrs": a named condition with the sheet's own hours.
    std::string buff;
    int buff_hours = 0;

    bool removed_when_used = false;  // "remove Item"
    int becomes_item = 0;            // "Change Item to N", or 0

    std::vector<MixResult> mixes;  // by column, aligned with the table's ids
};

enum class UseItemError : std::uint8_t {
    None,
    NoHeader,  // the id header row was not found
};

class UseItemTable {
public:
    [[nodiscard]] static UseItemError parse(const TextTable& table, UseItemTable& out);

    [[nodiscard]] const std::vector<UseItemEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const UseItemEntry* find(int item_id) const noexcept;

    // What pouring `first` into `second` gives, or None for pairs the table
    // does not know.
    [[nodiscard]] MixResult mix(int first_item, int second_item) const noexcept;

private:
    std::vector<UseItemEntry> entries_;
    std::vector<int> mix_ids_;  // the matrix's column ids, in column order
};

// The spell a scroll casts: the `S47` in its `ITEMS.TXT` first modifier,
// or 0 when the modifier is not an S-number.
[[nodiscard]] int scroll_spell_of(std::string_view modifier) noexcept;

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_USE_ITEMS_HPP
