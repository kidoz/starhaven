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

// Why a rest did not happen, or that it did.
enum class RestResult : std::uint8_t {
    Rested,
    // Something alive is close enough to object.
    Disturbed,
    // Nobody is left standing to make camp.
    NobodyStanding,
};

// Rest the party: eight hours, and everyone who is still standing wakes up
// whole. A character who is down stays down — getting back up is a temple's
// business, not a night's sleep. `inferred`
inline RestResult rest(std::array<Character, 4>& party, GameClock& clock, bool disturbed) {
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
    for (auto& who : party) {
        // The dead do not rest; the merely unconscious come to at a single
        // hit point rather than a full night's worth. `inferred`
        if (who.dead()) {
            continue;
        }
        if (who.hit_points <= 0) {
            who.hit_points = 1;
            continue;
        }
        who.hit_points = who.max_hit_points;
        who.spell_points = who.max_spell_points;
    }
    return RestResult::Rested;
}

// What to tell the player.
[[nodiscard]] inline std::string rest_message(RestResult result, const GameClock& clock) {
    switch (result) {
    case RestResult::Rested:
        return "You rest. It is " + clock.text();
    case RestResult::Disturbed:
        return "You cannot rest with monsters nearby";
    case RestResult::NobodyStanding:
        return "Nobody is left standing to make camp";
    }
    return {};
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_REST_HPP
