#ifndef STARHAVEN_GAME_ENCHANT_HPP
#define STARHAVEN_GAME_ENCHANT_HPP

// What a worn enchantment grants, on the bonus tables' own terms.
//
// A standard bonus row names its stat outright — "Might", "Fire
// Resistance", "Hit Points", "Armor Class" — and the generator rolled its
// strength, so applying it is a lookup. A special bonus writes prose, and
// the phrasings this reads are the shipped ones: "+10 to all Resistances",
// "Adds 6-8 points of Cold damage", "+10 Spell points". A special whose
// effect names none of these — Vampiric, the spell-school boosts — is
// carried as prose on the sheet and grants nothing yet, honestly.

#include <cctype>
#include <string>
#include <string_view>

#include "core/data/dice.hpp"
#include "core/data/item_generation.hpp"
#include "game/party.hpp"

namespace starhaven::game {

// Everything a single enchantment grants that this engine can apply.
struct EnchantPower {
    std::array<int, kAttributeCount> attributes{};
    std::array<int, data::kResistanceCount> resistances{};
    int armor_class = 0;
    int hit_points = 0;
    int spell_points = 0;
    data::Dice extra_damage{};      // a weapon rider, e.g. "of Frost"
    std::string damage_element;     // what answers it: "Cold", "Fire"...

    [[nodiscard]] bool any() const noexcept {
        for (const int a : attributes) {
            if (a != 0) {
                return true;
            }
        }
        for (const int r : resistances) {
            if (r != 0) {
                return true;
            }
        }
        return armor_class != 0 || hit_points != 0 || spell_points != 0 ||
               !extra_damage.empty();
    }
};

namespace enchant_detail {

// The attribute a stat name means, or kAttributeCount for none. The names
// are the standard-bonus table's own.
[[nodiscard]] inline std::size_t attribute_named(std::string_view stat) noexcept {
    constexpr std::array<std::pair<std::string_view, Attribute>, 7> kNames{
        {{"Might", Attribute::Might},
         {"Intellect", Attribute::Intellect},
         {"Personality", Attribute::Personality},
         {"Endurance", Attribute::Endurance},
         {"Accuracy", Attribute::Accuracy},
         {"Speed", Attribute::Speed},
         {"Luck", Attribute::Luck}}};
    for (const auto& [name, attribute] : kNames) {
        if (stat == name) {
            return static_cast<std::size_t>(attribute);
        }
    }
    return kAttributeCount;
}

// The resistance a name means, in the shared column order.
[[nodiscard]] inline std::size_t resistance_named(std::string_view name) noexcept {
    constexpr std::array<std::pair<std::string_view, data::Resistance>, 5> kNames{
        {{"Fire", data::Resistance::Fire},
         {"Elec", data::Resistance::Electricity},
         {"Cold", data::Resistance::Cold},
         {"Poison", data::Resistance::Poison},
         {"Magic", data::Resistance::Magic}}};
    for (const auto& [label, resistance] : kNames) {
        if (name.substr(0, label.size()) == label) {
            return static_cast<std::size_t>(resistance);
        }
    }
    return data::kResistanceCount;
}

}  // namespace enchant_detail

// A standard bonus at a rolled strength: the row's stat, the roll's amount.
[[nodiscard]] inline EnchantPower standard_power(const data::StandardBonusEntry& row,
                                                 int strength) {
    using namespace enchant_detail;
    EnchantPower out;
    if (const std::size_t a = attribute_named(row.stat); a < kAttributeCount) {
        out.attributes[a] = strength;
    } else if (row.stat == "Hit Points") {
        out.hit_points = strength;
    } else if (row.stat == "Spell Points") {
        out.spell_points = strength;
    } else if (row.stat == "Armor Class") {
        out.armor_class = strength;
    } else if (row.stat.find(" Resistance") != std::string::npos) {
        if (const std::size_t r = resistance_named(row.stat); r < data::kResistanceCount) {
            out.resistances[r] = strength;
        }
    }
    return out;
}

// A special bonus, from its prose where the phrasing is one this engine
// reads; anything else grants nothing and stays prose.
[[nodiscard]] inline EnchantPower special_power(const data::SpecialBonusEntry& row) {
    using namespace enchant_detail;
    EnchantPower out;
    const std::string& text = row.effect;

    // "+10 to all Resistances."
    if (const std::size_t at = text.find("+"); at != std::string::npos) {
        int amount = 0;
        std::size_t p = at + 1;
        while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) != 0) {
            amount = amount * 10 + (text[p] - '0');
            ++p;
        }
        const std::string_view rest = std::string_view(text).substr(p);
        if (amount > 0) {
            if (rest.find("all Resistances") != std::string_view::npos) {
                for (const auto resistance :
                     {data::Resistance::Fire, data::Resistance::Electricity,
                      data::Resistance::Cold, data::Resistance::Poison,
                      data::Resistance::Magic}) {
                    out.resistances[static_cast<std::size_t>(resistance)] = amount;
                }
            } else if (rest.find("Spell point") != std::string_view::npos) {
                out.spell_points = amount;
            } else if (rest.find("Hit point") != std::string_view::npos) {
                out.hit_points = amount;
            } else if (rest.find("AC") != std::string_view::npos ||
                       rest.find("Armor") != std::string_view::npos) {
                out.armor_class = amount;
            } else {
                // "+7 Might" and kin: the word after the number.
                std::size_t w = 0;
                while (w < rest.size() && rest[w] == ' ') {
                    ++w;
                }
                std::size_t e = w;
                while (e < rest.size() &&
                       std::isalpha(static_cast<unsigned char>(rest[e])) != 0) {
                    ++e;
                }
                if (const std::size_t a = attribute_named(rest.substr(w, e - w));
                    a < kAttributeCount) {
                    out.attributes[a] = amount;
                }
            }
        }
    }

    // "Adds 6-8 points of Cold damage."
    if (const std::size_t adds = text.find("Adds "); adds != std::string::npos) {
        std::size_t p = adds + 5;
        int low = 0;
        while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) != 0) {
            low = low * 10 + (text[p] - '0');
            ++p;
        }
        int high = low;
        if (p < text.size() && text[p] == '-') {
            high = 0;
            ++p;
            while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) != 0) {
                high = high * 10 + (text[p] - '0');
                ++p;
            }
        }
        const std::size_t of = text.find("points of ", p);
        if (low > 0 && high >= low && of != std::string::npos) {
            const std::string_view kind = std::string_view(text).substr(of + 10);
            if (const std::size_t r = resistance_named(kind); r < data::kResistanceCount) {
                // One die of (high - low + 1) sides offset by (low - 1)
                // reproduces the written range exactly.
                out.extra_damage = {1, high - low + 1, low - 1};
                out.damage_element = std::string(kind.substr(0, kind.find(' ')));
            }
        }
    }
    return out;
}

// The thing's full name once identified: the standard suffix rides after
// ("Longsword of Might"), a special affix beginning "of" does too, and any
// other affix leads ("Vampiric Longsword") — the affixes' own shapes.
[[nodiscard]] inline std::string enchanted_name(std::string_view base,
                                                const data::StandardBonusEntry* standard,
                                                const data::SpecialBonusEntry* special) {
    std::string name(base);
    if (standard != nullptr && !standard->name_suffix.empty()) {
        // The table writes the suffix as "(of Might)"-less prose already.
        name += " " + standard->name_suffix;
    } else if (special != nullptr && !special->name_affix.empty()) {
        if (special->name_affix.substr(0, 3) == "of ") {
            name += " " + special->name_affix;
        } else {
            name = special->name_affix + " " + name;
        }
    }
    return name;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_ENCHANT_HPP
