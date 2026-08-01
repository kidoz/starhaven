#ifndef STARHAVEN_GAME_TRAINING_HPP
#define STARHAVEN_GAME_TRAINING_HPP

// Turning experience into levels at a training hall.
//
// The rows of `2DEvents.txt` carry the halls' own numbers: the `Val` column
// scales the fee, and the first stock cell writes the ceiling the designers
// gave each hall — `"Max level = 15"`, `"No Max"` — with the sheet's margin
// note saying what the counter does: `"Train for Level (#) ... for (cost)"`.
// `observed` What no table gives is the experience a level requires and what
// a level grants; both are this engine's own and say so.

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/building_stats.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/text_table.hpp"
#include "game/party.hpp"
#include "game/shop.hpp"

namespace starhaven::game {

// Whether an establishment trains rather than trades.
[[nodiscard]] inline bool is_training(const data::BuildingStatsEntry& shop) noexcept {
    return shop.type == "Training";
}

// The hall's own ceiling, from `"Max level = 15"`. Zero means no ceiling —
// the sheet writes that hall's cell as `"No Max"`.
[[nodiscard]] inline int max_level_of(const data::BuildingStatsEntry& shop) noexcept {
    int level = 0;
    for (const char c : shop.stock_a) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            level = level * 10 + (c - '0');
        }
    }
    return level;
}

// The experience a level requires lives beside the class tables it pays
// for, in party.hpp — the same triangular curve, and still this engine's.

// What a hall charges to train to a level: its own `Val`, per level trained
// to. The margin note names the shape — `"for (cost)"` — and the scale is
// this engine's reading of `Val`. `inferred`
[[nodiscard]] inline int training_cost(const data::BuildingStatsEntry& shop, int to_level) noexcept {
    const auto cost = static_cast<int>(shop.price_factor * static_cast<float>(to_level));
    return cost < 1 ? 1 : cost;
}

// Whether this character can train here, and to what.
struct TrainingOffer {
    int to_level = 0;  // 0 when nothing is offered
    int cost = 0;
    int experience_needed = 0;  // what is still missing, 0 when ready
};

[[nodiscard]] inline TrainingOffer training_offer(const data::BuildingStatsEntry& shop,
                                                  const Character& who) noexcept {
    TrainingOffer offer;
    const int next = who.level + 1;
    const int ceiling = max_level_of(shop);
    if (ceiling > 0 && next > ceiling) {
        return offer;  // the hall does not teach that high
    }
    offer.to_level = next;
    offer.cost = training_cost(shop, next);
    const int required = experience_for_level(next);
    offer.experience_needed = who.experience < required ? required - who.experience : 0;
    return offer;
}

// The level's grant: hit points, and spell points for whoever casts. No
// table states the gains; these are this engine's own. `inferred`
inline void train(Character& who) {
    // A level is bought here and nowhere else: the level word at `+0x32` is
    // written by exactly two instructions in the whole executable, both of
    // them the generic "set a character field" and "add to a character
    // field" that map scripts use, and both capped at 255. **Nothing raises
    // a level automatically** — the hall is the only door. `observed`
    who.level += 1;
    level_up_to(who, who.level);
    // Skill points to spend on the sheet; that a level grants five is this
    // engine's own number. `inferred`
    who.skill_points += 5;
}

// --- Teaching: a new skill, and the two rungs above novice ----------------
//
// The teacher at `0x4969e4` is the only thing in the executable that sets a
// skill's rank bits, and it does three things and no more: it masks the byte
// with `0x3f`, ORs exactly one of `0x40` and `0x80`, and charges **2000 gold
// for expert or 5000 for master** (see skills.hpp for both traces). It does
// **not** test the points — a character with one point may be made a master
// if it can pay. `observed`
//
// What it does test is the gold, and which class is asking: the table at
// `0x4c2694` decides whether a class may hold the skill at all.

enum class TeachRefusal : std::uint8_t {
    None,
    ClassMayNot,   // the class's row zeroes this skill
    AlreadyHolds,  // asked to learn a skill already held
    NotHeld,       // asked to rank a skill not held
    NotNextRung,   // asked for master while still a novice
    TooPoor,
};

// What a guild charges to teach a skill outright. No table states it and no
// routine has been read for it; the hall's own `Val` is this engine's
// reading, the same one `training_cost` takes. `inferred`
[[nodiscard]] inline int learn_cost(const data::BuildingStatsEntry& shop) noexcept {
    const auto cost = static_cast<int>(shop.price_factor);
    return cost < 1 ? 1 : cost;
}

// Teach a skill the character does not have, at one point and no rank.
inline TeachRefusal learn_skill(Character& who, int slot, int cost, int& gold) {
    if (slot < 0 || slot >= kSkillSlots) {
        return TeachRefusal::ClassMayNot;
    }
    if (!class_may_learn(class_id(who.class_name), slot)) {
        return TeachRefusal::ClassMayNot;
    }
    const std::string name(kSkillNames[static_cast<std::size_t>(slot)]);
    if (const auto it = who.skills.find(name);
        it != who.skills.end() && skill_points(it->second) > 0) {
        return TeachRefusal::AlreadyHolds;
    }
    if (gold < cost) {
        return TeachRefusal::TooPoor;
    }
    gold -= cost;
    who.skills[name] = 1;
    return TeachRefusal::None;
}

// Buy the next rung. The teacher takes the rank it is told and clears the old
// bits first, so a rung is never both; this refuses a jump from novice
// straight to master, which is this engine's own rule and not the routine's.
// `inferred` for the one-rung-at-a-time part, `observed` for everything else.
inline TeachRefusal buy_rank(Character& who, int slot, int rank, int& gold) {
    if (slot < 0 || slot >= kSkillSlots) {
        return TeachRefusal::ClassMayNot;
    }
    if (!class_may_learn(class_id(who.class_name), slot)) {
        return TeachRefusal::ClassMayNot;
    }
    const std::string name(kSkillNames[static_cast<std::size_t>(slot)]);
    const auto it = who.skills.find(name);
    if (it == who.skills.end() || skill_points(it->second) <= 0) {
        return TeachRefusal::NotHeld;
    }
    if (rank < 1 || rank > 2 || rank != skill_rank(it->second) + 1) {
        return TeachRefusal::NotNextRung;
    }
    const int cost = teach_price(rank);
    if (gold < cost) {
        return TeachRefusal::TooPoor;
    }
    gold -= cost;
    it->second = teach_rank(it->second, rank);
    return TeachRefusal::None;
}

// **Which door teaches which skill.** No table says it outright, but two
// halves of it are readable and the third is the shape of the first two:
//
//  * a **magic guild's** row names its own school in the `Type = Fire`
//    cell that also stocks its shelves, so the Fire Guild teaches Fire —
//    `observed`, since the school is the row's own word;
//  * a **weapon or armour shop** stocks items whose `skill_group` is a
//    `SKILLDES.TXT` heading, so what it can teach is what it sells —
//    `inferred`, but from the shop's own shelf rather than from nothing;
//  * a **training hall** sells levels and nothing else, which is what its
//    `Max level` cell and its margin note describe.
//
// Everything else — Perception, Diplomacy, Learning and the rest — has no
// door in the tables at all, and is left without one rather than given an
// invented one. `unknown`
[[nodiscard]] inline bool teaches_skill(const data::BuildingStatsEntry& shop, int slot,
                                        const data::ItemStatsTable& items,
                                        const std::vector<StockItem>& stock) {
    if (slot < 0 || slot >= kSkillSlots) {
        return false;
    }
    const std::string_view wanted = kSkillNames[static_cast<std::size_t>(slot)];
    if (const GuildStock guild = parse_guild_stock(shop.stock_a); !guild.empty()) {
        return data::school_name(guild.school) == wanted;
    }
    for (const StockItem& held : stock) {
        const auto* row =
            held.item_id > 0 ? items.at(static_cast<std::size_t>(held.item_id)) : nullptr;
        if (row != nullptr && row->skill_group == wanted) {
            return true;
        }
    }
    return false;
}

// What a character may still be taught here, in slot order.
[[nodiscard]] inline std::vector<int> teachable_skills(const Character& who) {
    std::vector<int> out;
    for (int slot = 0; slot < kSkillSlots; ++slot) {
        if (!class_may_learn(class_id(who.class_name), slot)) {
            continue;
        }
        const auto it = who.skills.find(std::string(kSkillNames[static_cast<std::size_t>(slot)]));
        if (it == who.skills.end() || skill_points(it->second) <= 0) {
            out.push_back(slot);
        }
    }
    return out;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAINING_HPP
