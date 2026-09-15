#include "game/party_event.hpp"

namespace starhaven::game {

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
