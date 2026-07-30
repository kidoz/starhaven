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
#include "core/data/journal.hpp"
#include "core/data/spell_stats.hpp"
#include "core/data/use_items.hpp"
#include "core/random.hpp"
#include "core/world/map_event.hpp"

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
    int standard_bonus = 0;
    int standard_strength = 0;
    int special_bonus = 0;
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

// A magic guild's shelf, written in its own row: `"Type = Fire, Spells
// 1-7"` names the school and the within-school numbers it sells — 1..7 at
// an Initiate guild, 1..11 at an Adept — and `Val` (2 or 3) multiplies the
// books' own values. `observed`
struct GuildStock {
    data::SpellSchool school = data::SpellSchool::Count;
    int low = 0;
    int high = 0;

    [[nodiscard]] bool empty() const noexcept {
        return school == data::SpellSchool::Count || low <= 0 || high < low;
    }
};

// The award a guild's shelves ask for: `Awards.txt` rows 72..80 read
// "Joined the Fire Guild" through "Joined the Dark Guild", one per school —
// the same names the guilds' own `Type =` cells write. No shipped event
// sets them, so joining is the counter's sale; only the price is the
// engine's. `observed` for the rows, `inferred` for the sale.
[[nodiscard]] inline int guild_award_of(data::SpellSchool school,
                                        const data::JournalTable& awards) {
    const std::string wanted =
        "Joined the " + std::string(data::school_name(school)) + " Guild";
    for (const auto& row : awards.entries()) {
        if (row.text == wanted) {
            return row.bit;
        }
    }
    return 0;
}

// What joining costs: a hundred gold at the guild's own Val multiplier —
// the number is this engine's, the multiplier the row's. `inferred`
[[nodiscard]] inline int guild_dues(const data::BuildingStatsEntry& shop) noexcept {
    const int factor = shop.price_factor >= 1.0f ? static_cast<int>(shop.price_factor) : 1;
    return 100 * factor;
}

[[nodiscard]] inline GuildStock parse_guild_stock(std::string_view cell) noexcept {
    GuildStock out;
    const std::size_t type = cell.find("Type = ");
    const std::size_t spells = cell.find("Spells ");
    if (type == std::string_view::npos || spells == std::string_view::npos) {
        return out;
    }
    for (std::size_t school = 0; school < data::kSpellSchoolCount; ++school) {
        const std::string_view name = data::school_name(static_cast<data::SpellSchool>(school));
        if (cell.compare(type + 7, name.size(), name) == 0) {
            out.school = static_cast<data::SpellSchool>(school);
            break;
        }
    }
    std::size_t at = spells + 7;
    while (at < cell.size() && std::isdigit(static_cast<unsigned char>(cell[at])) != 0) {
        out.low = out.low * 10 + (cell[at] - '0');
        ++at;
    }
    if (at < cell.size() && cell[at] == '-') {
        ++at;
        while (at < cell.size() && std::isdigit(static_cast<unsigned char>(cell[at])) != 0) {
            out.high = out.high * 10 + (cell[at] - '0');
            ++at;
        }
    }
    if (out.high < out.low) {
        out.high = out.low;
    }
    return out;
}

// A bank rather than a shop: the sheet's own margin notes name its two
// actions, "Deposit" and "Withdraw", and no column carries an interest
// rate — so the vault only keeps what it is given.
[[nodiscard]] inline bool is_bank(const data::BuildingStatsEntry& shop) noexcept {
    return shop.type == "Bank";
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

// A guild's shelves: the books of its school's spells in its own range,
// priced at the books' values times the row's Val. Empty for anything that
// is not a magic guild, so the caller can fall back to generated stock.
[[nodiscard]] inline std::vector<StockItem>
guild_stock_of(const data::BuildingStatsEntry& shop, const data::SpellStatsTable& spells,
               const data::ItemStatsTable& items) {
    std::vector<StockItem> out;
    const GuildStock wanted = parse_guild_stock(shop.stock_a);
    if (wanted.empty()) {
        return out;
    }
    for (const auto& spell : spells.entries()) {
        if (spell.school != wanted.school || spell.number < wanted.low ||
            spell.number > wanted.high) {
            continue;
        }
        for (std::size_t id = 1; id < items.entries().size(); ++id) {
            const auto* row = items.at(id);
            if (row == nullptr || row->equip_type != data::ItemEquipType::Book) {
                continue;
            }
            if (data::scroll_spell_of(row->modifier_1) == spell.id) {
                out.push_back({static_cast<int>(id), asking_price(*row, shop.price_factor)});
                break;
            }
        }
    }
    return out;
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
            out.push_back({rolled.item_id, asking_price(*row, shop.price_factor),
                           rolled.standard_bonus, rolled.standard_bonus_strength,
                           rolled.special_bonus});
        }
    }
    return out;
}

// What a chest holds: its record's own slots, decoded from the map's event
// payload. A positive id is the designers' fixed item, carried with whatever
// enchantment the record wrote (the shipped templates write none). Ids
// −1..−6 defer to the generator in placeholder classes 1..6, joined with
// the map's 0..6 treasure class through the documented 6×7 level-range
// table (see docs/formats/items.md and event-tables.md). The seed keeps a
// chest's rolls its own; an unknown negative id is dropped, not invented.
[[nodiscard]] inline std::vector<data::GeneratedItem>
chest_contents(const std::vector<world::MapItemInstance>& slots,
               std::size_t map_treasure_class, const data::RandomItemTable& random_items,
               const data::ItemStatsTable& items, const data::StandardBonusTable& standard,
               const data::SpecialBonusTable& special, std::uint32_t seed) {
    std::vector<data::GeneratedItem> out;
    Mm6Random random{seed};
    data::ArtifactGenerationState artifacts;
    for (const auto& slot : slots) {
        if (slot.empty()) {
            continue;
        }
        if (slot.item_id > 0) {
            data::GeneratedItem fixed;
            fixed.item_id = slot.item_id;
            fixed.standard_bonus = slot.standard_bonus_or_potion_power;
            fixed.standard_bonus_strength = slot.standard_bonus_strength;
            fixed.special_bonus = slot.special_bonus_or_gold_amount;
            fixed.charges = slot.charges;
            fixed.identified = slot.identified();
            out.push_back(fixed);
            continue;
        }
        const int placeholder = slot.random_treasure_class();
        if (placeholder == 0) {
            continue;
        }
        const auto level = data::roll_chest_treasure_level(
            static_cast<std::size_t>(placeholder), map_treasure_class, random);
        if (!level) {
            continue;
        }
        data::GeneratedItem rolled;
        if (data::generate_random_item(random_items, items, standard, special,
                                       static_cast<std::size_t>(*level), random, artifacts,
                                       rolled) != data::ItemGenerationError::None) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(rolled.item_id));
        if (row != nullptr && !row->name.empty()) {
            out.push_back(rolled);
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
