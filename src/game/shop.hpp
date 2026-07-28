#ifndef STARHAVEN_GAME_SHOP_HPP
#define STARHAVEN_GAME_SHOP_HPP

// Buying and selling.
//
// A shop's row in `2DEvents.txt` gives it a price multiplier and two stock
// specifications written the designers' way — `"L1 Weap"`, `"L2 Sword,Dagger"`
// — plus a count. The level and the broad kind drive the item generator that
// already reproduces the original's random-item path, so a shop's shelves are
// generated rather than invented. What is invented is the purse, the price a
// shop pays when buying from you, and how the skill words in a stock spec map
// past the generator's own kinds; all three are marked where they are defined.

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/building_stats.hpp"
#include "core/data/item_generation.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/merchant_text.hpp"
#include "core/random.hpp"

namespace starhaven::game {

// What a party starts with. No table says. `inferred`
inline constexpr int kStartingGold = 200;

// What a shop pays for what you bring it, as a fraction of the item's value.
// The table gives the multiplier a shop charges, not the one it pays.
// `inferred`
inline constexpr float kSellFraction = 0.5f;

// One thing on a shelf.
struct StockItem {
    int item_id = 0;
    int price = 0;
};

// A stock specification: `"L2 Sword,Dagger"` is treasure level 2, weapons.
struct StockSpec {
    std::size_t level = 1;
    data::ItemGenerationType type = data::ItemGenerationType::Any;
};

// Read one. The leading `Ln` is the treasure level; the words after it name a
// kind. The generator knows equipment kinds, not weapon skills, so
// `"Sword,Dagger"` becomes `Weapon` and which sword is left to the roll.
// `inferred`
[[nodiscard]] inline StockSpec parse_stock(std::string_view text) noexcept {
    StockSpec out;
    const std::string_view trimmed = data::trim(text);
    std::size_t at = 0;
    if (at < trimmed.size() && (trimmed[at] == 'L' || trimmed[at] == 'l')) {
        ++at;
        std::size_t level = 0;
        while (at < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[at])) != 0) {
            level = level * 10 + static_cast<std::size_t>(trimmed[at] - '0');
            ++at;
        }
        if (level > 0) {
            out.level = level;
        }
    }
    const auto has = [trimmed](std::string_view word) {
        for (std::size_t i = 0; i + word.size() <= trimmed.size(); ++i) {
            bool same = true;
            for (std::size_t j = 0; j < word.size() && same; ++j) {
                same = std::tolower(static_cast<unsigned char>(trimmed[i + j])) ==
                       std::tolower(static_cast<unsigned char>(word[j]));
            }
            if (same) {
                return true;
            }
        }
        return false;
    };
    if (has("weap") || has("sword") || has("dagger") || has("axe") || has("mace") || has("staff") ||
        has("spear")) {
        out.type = data::ItemGenerationType::Weapon;
    } else if (has("bow")) {
        out.type = data::ItemGenerationType::Missile;
    } else if (has("armor") || has("leather") || has("chain") || has("plate")) {
        out.type = data::ItemGenerationType::Armor;
    } else if (has("shield")) {
        out.type = data::ItemGenerationType::Shield;
    } else if (has("helm")) {
        out.type = data::ItemGenerationType::Helm;
    } else if (has("potion")) {
        out.type = data::ItemGenerationType::Potion;
    } else if (has("wand")) {
        out.type = data::ItemGenerationType::Wand;
    } else if (has("scroll")) {
        out.type = data::ItemGenerationType::SpellScroll;
    } else if (has("ring") || has("amulet")) {
        out.type = data::ItemGenerationType::Ring;
    }
    return out;
}

// What a shop asks for an item: its value times the shop's own multiplier,
// never less than one gold.
[[nodiscard]] inline int asking_price(const data::ItemStatsEntry& item, float factor) noexcept {
    const auto price = static_cast<int>(static_cast<float>(item.value) * factor);
    return price < 1 ? 1 : price;
}

// And what it pays.
[[nodiscard]] inline int offer_price(const data::ItemStatsEntry& item) noexcept {
    const auto price = static_cast<int>(static_cast<float>(item.value) * kSellFraction);
    return price < 1 ? 1 : price;
}

// Fill a shop's shelves. The seed is the shop's own row id, so a shop holds
// the same stock every time the game is started and restocks only when
// something makes it.
[[nodiscard]] inline std::vector<StockItem>
stock_of(const data::BuildingStatsEntry& shop, const data::RandomItemTable& random_items,
         const data::ItemStatsTable& items, const data::StandardBonusTable& standard,
         const data::SpecialBonusTable& special, std::uint32_t seed) {
    std::vector<StockItem> out;
    Mm6Random random{seed};
    data::ArtifactGenerationState artifacts;

    // How many the row's third column asks for, defaulting to a handful.
    int wanted = 0;
    for (const char c : shop.stock_c) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            wanted = wanted * 10 + (c - '0');
        }
    }
    if (wanted <= 0) {
        wanted = 6;  // `inferred`: the column is blank on many rows
    }

    for (const std::string_view text :
         {std::string_view(shop.stock_a), std::string_view(shop.stock_b)}) {
        if (data::trim(text).empty()) {
            continue;
        }
        const StockSpec spec = parse_stock(text);
        for (int i = 0; i < wanted; ++i) {
            data::GeneratedItem rolled;
            if (data::generate_random_item(random_items, items, standard, special, spec.level,
                                           spec.type, random, artifacts,
                                           rolled) != data::ItemGenerationError::None) {
                continue;
            }
            const auto* row = items.at(static_cast<std::size_t>(rolled.item_id));
            if (row == nullptr || row->name.empty()) {
                continue;
            }
            out.push_back({rolled.item_id, asking_price(*row, shop.price_factor)});
        }
    }
    return out;
}

// Roll what a chest holds, from the map's own treasure level. The seed is the
// chest's own index, so a chest holds the same things however often it is
// looked at, and a different set from the chest beside it.
[[nodiscard]] inline std::vector<int>
chest_contents(std::size_t treasure_level, const data::RandomItemTable& random_items,
               const data::ItemStatsTable& items, const data::StandardBonusTable& standard,
               const data::SpecialBonusTable& special, std::uint32_t seed, int count) {
    std::vector<int> out;
    Mm6Random random{seed};
    data::ArtifactGenerationState artifacts;
    for (int i = 0; i < count; ++i) {
        data::GeneratedItem rolled;
        if (data::generate_random_item(random_items, items, standard, special, treasure_level,
                                       random, artifacts,
                                       rolled) != data::ItemGenerationError::None) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(rolled.item_id));
        if (row != nullptr && !row->name.empty()) {
            out.push_back(rolled.item_id);
        }
    }
    return out;
}

// The video family a shop's trade uses, from the names in `Anims1.vid` and
// `Anims2.vid`: thirteen trades each have a poor, middling and rich backdrop.
// See docs/formats/vid.md. The pairing is read off the names — "Blcks" for a
// weapon shop's blacksmith, "Arm" for an armourer — so it is `inferred`.
[[nodiscard]] inline std::string_view video_family(std::string_view type) noexcept {
    if (type == "Weapon Shop") {
        return "Blcks";
    }
    if (type == "Armor Shop") {
        return "Arm";
    }
    if (type == "Magic Shop") {
        return "Mag";
    }
    if (type == "General Store") {
        return "Genst";
    }
    if (type == "Tavern") {
        return "Tav";
    }
    if (type == "Temple") {
        return "Temp";
    }
    if (type == "Town Hall" || type == "City Council") {
        return "City";
    }
    if (type == "Thieves Guild") {
        return "Thf";
    }
    if (type == "Merc Guild") {
        return "Merc";
    }
    if (type == "The Oracle" || type == "The Seer") {
        return "Orac";
    }
    return {};
}

// And the whole name, at the quality the row's Picture column gives: 1, 2 and
// 3 are the tiers. The suffixes are not spelled consistently across families —
// "BlcksPor" against "ArmPoor", "Tavmid" against "TempMid" — so a caller has
// to try what the archive actually holds.
[[nodiscard]] inline std::vector<std::string> video_names(std::string_view type, int picture) {
    const std::string_view family = video_family(type);
    if (family.empty() || picture < 1 || picture > 3) {
        return {};
    }
    static constexpr std::array<std::array<const char*, 3>, 3> kSuffixes{
        {{"Por", "Poor", "poor"}, {"Mid", "mid", "MID"}, {"Rch", "Rich", "rich"}}};
    std::vector<std::string> out;
    for (const char* suffix : kSuffixes[static_cast<std::size_t>(picture - 1)]) {
        out.push_back(std::string(family) + suffix);
    }
    return out;
}

// Which line the shopkeeper says. The table has one for a merchant of the
// wrong type and one for an empty purse; the rest turn on a haggling skill
// nothing has yet, so the party is treated as unskilled. `inferred`
[[nodiscard]] inline std::string_view merchant_line(const data::MerchantTextTable& words,
                                                    data::MerchantAction action, bool affordable) {
    return words.line(affordable ? data::MerchantSituation::NoSkill
                                 : data::MerchantSituation::NotEnoughGold,
                      action);
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SHOP_HPP
