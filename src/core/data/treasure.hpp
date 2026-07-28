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
#include "core/data/item_generation.hpp"
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

// The kind a code names — `Bow`, `Ring`, `Misc` — is a kind the item
// generator already takes as a selector, so the word maps straight onto it.
// An empty or unrecognized kind is any kind. `inferred`
[[nodiscard]] inline ItemGenerationType treasure_item_type(std::string_view kind) noexcept {
    const auto is = [kind](std::string_view word) {
        if (kind.size() != word.size()) {
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(kind[i])) != word[i]) {
                return false;
            }
        }
        return true;
    };
    if (is("misc")) {
        return ItemGenerationType::Misc;
    }
    if (is("sword")) {
        return ItemGenerationType::Sword;
    }
    if (is("dagger")) {
        return ItemGenerationType::Dagger;
    }
    if (is("axe")) {
        return ItemGenerationType::Axe;
    }
    if (is("spear")) {
        return ItemGenerationType::Spear;
    }
    if (is("bow")) {
        return ItemGenerationType::Bow;
    }
    if (is("mace")) {
        return ItemGenerationType::Mace;
    }
    if (is("club")) {
        return ItemGenerationType::Club;
    }
    if (is("staff")) {
        return ItemGenerationType::Staff;
    }
    if (is("weapon")) {
        return ItemGenerationType::WeaponCategory;
    }
    if (is("armor")) {
        return ItemGenerationType::ArmorCategory;
    }
    if (is("leather")) {
        return ItemGenerationType::Leather;
    }
    if (is("chain")) {
        return ItemGenerationType::Chain;
    }
    if (is("plate")) {
        return ItemGenerationType::Plate;
    }
    if (is("shield")) {
        return ItemGenerationType::ShieldCategory;
    }
    if (is("cape")) {  // the codes write a cloak the way STDITEMS heads it
        return ItemGenerationType::CloakCategory;
    }
    if (is("ring")) {
        return ItemGenerationType::Ring;
    }
    if (is("amulet")) {
        return ItemGenerationType::Amulet;
    }
    if (is("wand")) {
        return ItemGenerationType::Wand;
    }
    return ItemGenerationType::Any;
}

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_TREASURE_HPP
