#ifndef STARHAVEN_CORE_DATA_TREASURE_HPP
#define STARHAVEN_CORE_DATA_TREASURE_HPP

// `MONSTERS.TXT`'s treasure column: what a kill leaves behind.
//
// The codes read `"5%6D20+L2Bow"` — a percentage chance, a roll of gold, and
// an item of a treasure level with an optional kind. Any part may be absent:
// `"4D6"` is gold alone, `"5%L2Ring"` a chance at a ring and nothing else.
// See docs/formats/text-tables.md.

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/data/dice.hpp"
#include "core/data/text_table.hpp"

namespace starhaven::data {

struct Treasure {
    int chance = 100;       // percent; absent means it always drops
    Dice gold;              // absent means no gold
    int item_level = 0;     // 0 means no item
    std::string item_kind;  // "Bow", "Ring", "Misc"; empty means any

    [[nodiscard]] bool empty() const noexcept { return gold.empty() && item_level == 0; }
};

// Parse one cell. A cell of "0" or nothing gives an empty drop.
[[nodiscard]] inline Treasure parse_treasure(std::string_view text) noexcept {
    Treasure out;
    const std::string_view s = trim(text);
    std::size_t at = 0;
    const auto number = [&s, &at]() {
        int v = -1;
        while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at])) != 0) {
            v = (v < 0 ? 0 : v) * 10 + (s[at] - '0');
            ++at;
        }
        return v;
    };

    const std::size_t start = at;
    const int first = number();
    if (first < 0) {
        return out;
    }
    if (at < s.size() && s[at] == '%') {
        out.chance = first;
        ++at;
    } else {
        at = start;  // it was the dice count, not a chance
    }

    // Gold, if the next thing is a roll.
    const std::size_t dice_start = at;
    while (at < s.size() && s[at] != '+' && s[at] != 'L' && s[at] != 'l') {
        ++at;
    }
    if (at > dice_start) {
        out.gold = parse_dice(s.substr(dice_start, at - dice_start));
    }

    // And an item, if a level follows.
    if (at < s.size() && s[at] == '+') {
        ++at;
    }
    if (at < s.size() && (s[at] == 'L' || s[at] == 'l')) {
        ++at;
        if (const int level = number(); level > 0) {
            out.item_level = level;
            out.item_kind = std::string(s.substr(at));
        }
    }
    return out;
}

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_TREASURE_HPP
