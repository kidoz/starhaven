#ifndef STARHAVEN_GAME_PARTY_EVENT_HPP
#define STARHAVEN_GAME_PARTY_EVENT_HPP

#include <array>
#include <cstdint>

#include "game/party.hpp"
#include "game/script_message.hpp"

namespace starhaven::game {

// The existing interaction policy uses the selected sheet member, or member
// zero. Capture their class at entry; script writes survive modal/riddle
// continuations, even if the displayed member changes during the pause.
[[nodiscard]] WalkOutcome walk_party_event(const world::MapScript& script,
                                           ScriptContinuation& request, WalkState& state,
                                           const std::array<Character, 4>& party,
                                           int selected_member,
                                           ScriptItemGenerator* generator = nullptr);

// The walker has already applied base gold and payments. Apply the best
// hireling's found-gold bonus, publish the live purse, and return the amount
// to display. Ordinary gold transfers do not receive this bonus.
[[nodiscard]] std::int64_t settle_script_gold(WalkState& state, const WalkOutcome& outcome,
                                              int bonus_percent, int& purse);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_PARTY_EVENT_HPP
