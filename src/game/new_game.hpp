#ifndef STARHAVEN_GAME_NEW_GAME_HPP
#define STARHAVEN_GAME_NEW_GAME_HPP

#include <array>

#include "game/party.hpp"
#include "game/save.hpp"

namespace starhaven::game {

// A week of rations to begin with is this engine's own choice; the tables
// do not say.
inline constexpr int kStartingFood = 7;

// Whether a shaped party may open the world: every member named, classed
// from the starting six, faced, rolled, and standing at full vitality.
[[nodiscard]] bool party_ready(const std::array<Character, 4>& party) noexcept;

// Build one fresh campaign as the same `SaveState` a save file carries, so
// the loading boundary cannot tell a new game from a loaded one: the traced
// New Sorpigal spawn, the purse and a week of food, and — when asked — the
// opening quest's letter and its lit bit. On an incomplete party `out` is
// untouched and false comes back.
[[nodiscard]] bool make_new_game_state(const std::array<Character, 4>& party,
                                       bool seed_opening_quest, SaveState& out);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_NEW_GAME_HPP
