#include "game/party_event.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace starhaven;
using namespace starhaven::game;
using namespace starhaven::world;

namespace {

MapScript script(const std::vector<ScriptStep>& steps) {
    std::vector<std::byte> bytes(48, std::byte{0});
    for (const auto& step : steps) {
        bytes.push_back(static_cast<std::byte>(4 + step.arguments.size()));
        bytes.push_back(static_cast<std::byte>(step.event_id & 0xffU));
        bytes.push_back(static_cast<std::byte>(step.event_id >> 8U));
        bytes.push_back(static_cast<std::byte>(step.sequence));
        bytes.push_back(static_cast<std::byte>(step.opcode));
        for (const auto byte : step.arguments) {
            bytes.push_back(static_cast<std::byte>(byte));
        }
    }
    MapScript out;
    REQUIRE(MapScript::parse(bytes, out) == MapScriptError::None);
    return out;
}

std::array<Character, 4> party() {
    std::array<Character, 4> result;
    result[0].class_name = "Sorcerer";
    result[1].class_name = "Crusader";
    result[2].class_name = "Cleric";
    result[3].class_name = "Archer";
    return result;
}

ScriptActorContext actor_of(const ScriptContinuation& request) {
    // An absent context must fail the member/class assertions below.
    return request.actor.value_or(ScriptActorContext{4, -1});
}

}  // namespace

TEST_CASE("class gates use the live selected member and refresh for each interaction",
          "[party-event]") {
    const auto events = script({
        {1, 0, kOpcodeCheck, {kVarClass, 10, 0, 0, 0, 3}},
        {1, 1, kOpcodeGive, {kVarAward, 2, 0, 0, 0}},
        {1, 2, kOpcodeEnd, {}},
        {1, 3, kOpcodeGive, {kVarAward, 1, 0, 0, 0}},
        {1, 4, kOpcodeEnd, {}},
    });
    WalkState state;
    state.variables[kVarClass] = 0;
    ScriptContinuation selected{"Synthetic.blv", 1, -1, false, false, {}};
    REQUIRE(walk_party_event(events, selected, state, party(), 1).ran);
    REQUIRE(state.awards == std::set<int>{1});
    REQUIRE(actor_of(selected).member == 1);
    REQUIRE(actor_of(selected).class_value == 10);

    for (const int selection : {-1, 0, 2, 99}) {
        CAPTURE(selection);
        state.awards.clear();
        ScriptContinuation next{"Synthetic.blv", 1, -1, false, false, {}};
        REQUIRE(walk_party_event(events, next, state, party(), selection).ran);
        REQUIRE(state.awards == std::set<int>{2});
        REQUIRE(actor_of(next).member == (selection == 2 ? std::size_t{2} : std::size_t{0}));
        REQUIRE(actor_of(next).class_value == (selection == 2 ? 3 : 6));
    }
}

TEST_CASE("class writes and acting member survive a modal continuation", "[party-event]") {
    const auto events = script({
        {1, 0, kOpcodeSet, {kVarClass, 11, 0, 0, 0}},
        {1, 1, kOpcodeLongMessage, {0}},
        {1, 2, kOpcodeShowMessage, {}},
        {1, 3, kOpcodeCheck, {kVarClass, 11, 0, 0, 0, 6}},
        {1, 4, kOpcodeGive, {kVarAward, 2, 0, 0, 0}},
        {1, 5, kOpcodeEnd, {}},
        {1, 6, kOpcodeGive, {kVarAward, 1, 0, 0, 0}},
        {1, 7, kOpcodeEnd, {}},
    });
    WalkState state;
    ScriptContinuation request{"Synthetic.blv", 1, -1, true, false, {}};
    const auto first = walk_party_event(events, request, state, party(), 1);
    if (!first.message) {
        FAIL("expected a modal continuation");
        return;
    }
    REQUIRE(state.awards.empty());
    request.sequence = first.message->resume_at;
    ScriptMessage modal;
    modal.show(request, "Synthetic message");
    modal.dismiss();
    auto resumed = modal.take_resume("SYNTHETIC.BLV");
    if (!resumed) {
        FAIL("expected a dismissed modal to resume");
        return;
    }
    state.variables[kVarClass] = 0;
    REQUIRE(walk_party_event(events, *resumed, state, party(), 2).ran);
    REQUIRE(actor_of(*resumed).member == 1);
    REQUIRE(actor_of(*resumed).class_value == 11);
    REQUIRE(state.awards == std::set<int>{1});
}

TEST_CASE("riddle continuations retain their actor and script-written class", "[party-event]") {
    const auto events = script({
        {1, 0, kOpcodeSet, {kVarClass, 11, 0, 0, 0}},
        {1, 1, kOpcodeAsk, {0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 4}},
        {1, 2, kOpcodeGive, {kVarAward, 3, 0, 0, 0}},
        {1, 3, kOpcodeEnd, {}},
        {1, 4, kOpcodeCheck, {kVarClass, 11, 0, 0, 0, 7}},
        {1, 5, kOpcodeGive, {kVarAward, 2, 0, 0, 0}},
        {1, 6, kOpcodeEnd, {}},
        {1, 7, kOpcodeGive, {kVarAward, 1, 0, 0, 0}},
        {1, 8, kOpcodeEnd, {}},
    });
    for (const bool answer_matches : {false, true}) {
        CAPTURE(answer_matches);
        WalkState state;
        ScriptContinuation request{"Synthetic.blv", 1, -1, false, false, {}};
        const auto first = walk_party_event(events, request, state, party(), 1);
        if (!first.ask) {
            FAIL("expected a riddle continuation");
            return;
        }
        REQUIRE(state.awards.empty());
        request.sequence = answer_matches ? first.ask->step_on_match : first.ask->step_on_miss;
        state.variables[kVarClass] = 0;
        REQUIRE(walk_party_event(events, request, state, party(), 0).ran);
        REQUIRE(actor_of(request).member == 1);
        REQUIRE(state.awards == std::set<int>{answer_matches ? 1 : 3});
    }
}
