#ifndef STARHAVEN_GAME_REST_HPP
#define STARHAVEN_GAME_REST_HPP

// Sleeping it off.

#include <array>
#include <string>

#include "game/clock.hpp"
#include "game/party.hpp"

namespace starhaven::game {

// How near a living monster stops the party sleeping, in world units. Twice
// the distance one can reach you from. `inferred`
inline constexpr float kRestDisturbance = 1600.0f;

// What a camp eats, in days of food. The tables never state it, but the
// professions bound it: a Quarter Master saves "two less days of food use
// when camping, (minimum of one used)", and a reduction of two with a floor
// of one only matters if the base is at least three. Three it is. `inferred`
// from the rows' own arithmetic.
inline constexpr int kRestFoodCost = 3;

// Why a rest did not happen, or how it went.
enum class RestResult : std::uint8_t {
    Rested,
    // The larder was empty: the night passes but heals nothing, and everyone
    // standing wakes Weak. That hunger costs the night's good is this
    // engine's reading of camping on an empty stomach. `inferred`
    Starved,
    // Something alive is close enough to object.
    Disturbed,
    // Nobody is left standing to make camp.
    NobodyStanding,
};

// Rest the party: eight hours, and everyone who is still standing wakes up
// whole — if the party can eat. `food_cost` is what this camp consumes, the
// caller's to reduce by a Porter's or a Gypsy's savings.
inline RestResult rest(std::array<Character, 4>& party, GameClock& clock, bool disturbed,
                       int& food, int food_cost) {
    if (disturbed) {
        return RestResult::Disturbed;
    }
    bool standing = false;
    for (const auto& who : party) {
        standing = standing || who.hit_points > 0;
    }
    if (!standing) {
        return RestResult::NobodyStanding;
    }

    clock.advance_hours(kRestHours);
    const bool fed = food >= food_cost;
    food = fed ? food - food_cost : 0;
    for (auto& who : party) {
        // The dead do not rest; the merely unconscious come to at a single
        // hit point rather than a full night's worth. `inferred`
        if (who.dead()) {
            continue;
        }
        if (!fed) {
            if (who.hit_points > 0 && who.affliction.empty()) {
                who.affliction = "Weak";
            }
            continue;
        }
        if (who.hit_points <= 0) {
            who.hit_points = 1;
            continue;
        }
        who.hit_points = who.max_hit_points;
        who.spell_points = who.max_spell_points;
    }
    return fed ? RestResult::Rested : RestResult::Starved;
}

// A camp with food enough, for callers that do not track a larder.
inline RestResult rest(std::array<Character, 4>& party, GameClock& clock, bool disturbed) {
    int pantry = kRestFoodCost;
    return rest(party, clock, disturbed, pantry, kRestFoodCost);
}

// What to tell the player.
[[nodiscard]] inline std::string rest_message(RestResult result, const GameClock& clock) {
    switch (result) {
    case RestResult::Rested:
        return "You rest. It is " + clock.text();
    case RestResult::Starved:
        return "You camp without food and wake weak. It is " + clock.text();
    case RestResult::Disturbed:
        return "You cannot rest with monsters nearby";
    case RestResult::NobodyStanding:
        return "Nobody is left standing to make camp";
    }
    return {};
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_REST_HPP
