// Original synthetic event records; no game resources.
#include <catch2/catch_test_macros.hpp>

#include "game/script_coverage.hpp"
#include "game/script_message.hpp"

using namespace starhaven::world;
using namespace starhaven::game;

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
    MapScript result;
    REQUIRE(MapScript::parse(bytes, result) == MapScriptError::None);
    return result;
}

}  // namespace

TEST_CASE("messages suspend rewards until acknowledgement and resume once", "[messages]") {
    const auto events = script({
        {7, 0, kOpcodeLongMessage, {42, 1, 0, 0}},
        {7, 1, kOpcodeShowMessage, {}},
        {7, 2, kOpcodeGive, {kVarGold, 19, 0, 0, 0}},
        {7, 3, kOpcodeSetTopic, {2, 0, 0, 0, 1, 9, 0, 0, 0}},
        {7, 4, kOpcodeEnd, {}},
    });
    WalkState state;
    WalkPresentation text;
    const auto first = walk_event(events, 7, state, -1, "Test.blv", &text);
    if (!first.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(first.message->text == 298);
    REQUIRE(first.message->resume_at == 2);
    REQUIRE(first.acted());
    REQUIRE(state.gold == 0);
    REQUIRE(state.npc_topics.empty());
    ScriptMessage modal;
    modal.show({"Test.blv", 7, first.message->resume_at, false, false, text}, "Synthetic text");
    REQUIRE_FALSE(modal.take_resume("Test.blv"));
    modal.dismiss();
    REQUIRE(modal.active());
    const auto resume = modal.take_resume("TEST.BLV");
    if (!resume) {
        FAIL("acknowledgement must produce one continuation");
        return;
    }
    const auto last = walk_event(events, resume->event, state, resume->sequence, resume->map);
    REQUIRE_FALSE(last.message);
    REQUIRE(state.gold == 19);
    REQUIRE(state.npc_topics.at({2, 1}) == 9);
    REQUIRE_FALSE(modal.active());
    REQUIRE_FALSE(modal.take_resume("Test.blv"));
}

TEST_CASE("messages use the reached text selection and preserve it across pauses", "[messages]") {
    const auto events = script({
        {1, 0, kOpcodeGoto, {2}},
        {1, 1, kOpcodeLongMessage, {9}},
        {1, 2, kOpcodeLongMessage, {7}},
        {1, 3, kOpcodeMessage, {6}},
        {1, 4, kOpcodeShowMessage, {255}},
        {1, 5, kOpcodeShowMessage, {}},
        {1, 6, kOpcodeEnd, {}},
    });
    WalkState state;
    WalkPresentation text;
    const auto first = walk_event(events, 1, state, -1, "Test.blv", &text);
    if (!first.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(first.message->text == 7);  // local status text does not replace long text
    const auto second = walk_event(events, 1, state, first.message->resume_at, "Test.blv", &text);
    if (!second.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(second.message->text == 7);
    REQUIRE(second.message->resume_at == 6);
    const auto npc = walk_event(events, 1, state, -1, "GLOBAL.EVT");
    if (!npc.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(npc.message->text == 6);  // NPC text selection uses both text opcodes
}

TEST_CASE("message continuation retains global bank and cancels after map changes", "[messages]") {
    ScriptMessage modal;
    modal.show({"Same.blv", 1, 2, true, true, {23}}, "NPC text");
    modal.dismiss();
    const auto resume = modal.take_resume("same.blv");
    if (!resume) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(resume->global);
    REQUIRE(resume->npc_dialogue);
    REQUIRE(resume->presentation.message == 23);
    modal.show(*resume, "Again");
    modal.dismiss();
    REQUIRE_FALSE(modal.take_resume("Other.blv"));
    REQUIRE_FALSE(modal.active());
}

TEST_CASE("message padding is ignored and empty payloads are valid", "[messages]") {
    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{{}, {0}, {1}, {255, 0, 99}}) {
        WalkState state;
        const auto events = script({{1, 0, kOpcodeShowMessage, bytes}});
        const auto out = walk_event(events, 1, state);
        if (!out.message) {
            FAIL("expected a message or continuation");
            return;
        }
        REQUIRE(out.message->text == -1);
        REQUIRE(out.unsupported.empty());
    }
    REQUIRE(script_opcode_coverage(kOpcodeShowMessage).minimum_arguments == 0);
}

TEST_CASE("message sequence 255 terminates without wrapping or replaying rewards", "[messages]") {
    const auto events = script({
        {1, 0, kOpcodeGive, {kVarGold, 8, 0, 0, 0}},
        {1, 255, kOpcodeShowMessage, {}},
    });
    WalkState state;
    const auto first = walk_event(events, 1, state);
    if (!first.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE(first.message->resume_at == 256);
    REQUIRE(state.gold == 8);
    const auto resumed = walk_event(events, 1, state, first.message->resume_at);
    REQUIRE_FALSE(resumed.message);
    REQUIRE(state.gold == 8);
}

TEST_CASE("message continuation uses sequence numbers and respects disabled events", "[messages]") {
    const auto events = script({
        {1, 0, kOpcodeShowMessage, {}},
        {1, 3, kOpcodeGive, {kVarGold, 8, 0, 0, 0}},
    });
    WalkState state;
    const auto first = walk_event(events, 1, state);
    if (!first.message) {
        FAIL("expected a message or continuation");
        return;
    }
    REQUIRE_FALSE(walk_event(events, 1, state, first.message->resume_at).acted());
    REQUIRE(state.gold == 0);
    state.disabled_events["test.blv"].insert(1);
    REQUIRE_FALSE(walk_event(events, 1, state, 0, "TEST.BLV").ran);
}
