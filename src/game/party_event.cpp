#include "game/party_event.hpp"

#include <algorithm>
#include <limits>

namespace starhaven::game {

std::int64_t settle_script_gold(WalkState& state, const WalkOutcome& outcome, int bonus_percent,
                                int& purse) {
    const auto found = std::max<std::int64_t>(0, outcome.gold_found);
    const auto bonus = found * std::max(0, bonus_percent) / 100;
    const auto room = std::int64_t{std::numeric_limits<int>::max()} - state.gold;
    const auto paid_bonus = std::min(bonus, room);
    state.gold = static_cast<int>(std::int64_t{state.gold} + paid_bonus);
    purse = state.gold;
    return found + paid_bonus;
}

WalkOutcome walk_party_event(const world::MapScript& script, ScriptContinuation& request,
                             WalkState& state, const std::array<Character, 4>& party,
                             int selected_member, ScriptItemGenerator* generator) {
    if (!request.actor) {
        const auto member =
            selected_member >= 0 && static_cast<std::size_t>(selected_member) < party.size()
                ? static_cast<std::size_t>(selected_member)
                : 0;
        request.actor = ScriptActorContext{member, class_id(party[member].class_name)};
    }
    state.variables[world::kVarClass] = request.actor->class_value;
    auto outcome = walk_event(script, request.event, state, request.sequence,
                              request.global ? "GLOBAL.EVT" : request.map, &request.presentation,
                              generator, request.decoration);
    request.actor->class_value = state.variables[world::kVarClass];
    return outcome;
}

}  // namespace starhaven::game
