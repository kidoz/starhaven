#ifndef STARHAVEN_GAME_TRAPS_HPP
#define STARHAVEN_GAME_TRAPS_HPP

// Chests that bite back, gated by the map's own numbers.
//
// `MapStats.txt` gives every map a trap difficulty in a column headed
// "Trap 0-10"; the skills table gives Disarm Traps "increases chance to
// disarm traps on chests" and Perception "increases chance to avoid traps".
// Which chests are trapped, what a blast rolls, and how the skills' chances
// scale are this engine's own readings and say so below; the difficulty and
// the two skills' jobs are the tables'.

#include <array>
#include <cstdint>

#include "core/random.hpp"
#include "game/party.hpp"

namespace starhaven::game {

// Whether this chest is trapped at all: the difficulty read as a chance in
// ten, salted by the chest's own id so the same chest always answers the
// same way. `inferred` for reading the column as a chance.
[[nodiscard]] inline bool chest_trapped(int difficulty, int chest_id) {
    if (difficulty <= 0) {
        return false;
    }
    const auto salt = static_cast<std::uint32_t>(chest_id + 1) * 2654435761U;
    return static_cast<int>(salt % 10) < difficulty;
}

// Disarming: the best Disarm Traps in the party against the map's number,
// point for point. Reaching it defuses outright. `inferred`
[[nodiscard]] inline bool disarmed(int difficulty, int best_disarm) noexcept {
    return best_disarm >= difficulty;
}

// The blast, on everyone: the difficulty's worth of d6 each, avoided
// entirely at five percent a point of Perception. Both scales are the
// engine's own. `inferred`
struct TrapBlast {
    std::array<int, 4> damage{};  // per member; zero where avoided or down
};

[[nodiscard]] inline TrapBlast spring_trap(int difficulty, const std::array<Character, 4>& party,
                                           int best_perception, Mm6Random& random) {
    TrapBlast out;
    for (std::size_t i = 0; i < party.size(); ++i) {
        if (party[i].hit_points <= 0) {
            continue;
        }
        const int avoid = best_perception * 5 > 95 ? 95 : best_perception * 5;
        if (static_cast<int>(random.next() % 100) < avoid) {
            continue;
        }
        int rolled = 0;
        for (int d = 0; d < difficulty; ++d) {
            rolled += 1 + static_cast<int>(random.next() % 6);
        }
        out.damage[i] = rolled;
    }
    return out;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAPS_HPP
