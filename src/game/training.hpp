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
#include <string_view>

#include "core/data/building_stats.hpp"
#include "core/data/text_table.hpp"
#include "game/party.hpp"

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

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAINING_HPP
