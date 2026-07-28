// Tests for walking an event: checks that jump, gives and takes that move
// the party's things, and the flows the shipped scripts actually use.
//
// Hermetic: the scripts are built by hand from the format described in
// docs/formats/map-events.md.
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "game/script_walk.hpp"

using namespace starhaven;
using namespace starhaven::game;
using namespace starhaven::world;

namespace {

std::vector<std::byte> wrap(const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> out(48, std::byte{0});
    for (const std::uint8_t b : payload) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

void push_step(std::vector<std::uint8_t>& p, std::uint16_t id, std::uint8_t sequence,
               std::uint8_t opcode, const std::vector<std::uint8_t>& args) {
    p.push_back(static_cast<std::uint8_t>(4 + args.size()));
    p.push_back(static_cast<std::uint8_t>(id & 0xFF));
    p.push_back(static_cast<std::uint8_t>(id >> 8));
    p.push_back(sequence);
    p.push_back(opcode);
    for (const std::uint8_t a : args) {
        p.push_back(a);
    }
}

std::vector<std::uint8_t> typed(std::uint8_t type, std::uint32_t value) {
    return {type, static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
}

std::vector<std::uint8_t> check(std::uint8_t type, std::uint32_t value, std::uint8_t target) {
    auto out = typed(type, value);
    out.push_back(target);
    return out;
}

MapScript parse(const std::vector<std::uint8_t>& payload) {
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);
    return script;
}

}  // namespace

TEST_CASE("a switch throws once", "[walk]") {
    // The shipped shape: check its own variable, jump to the end when it is
    // already set, and set it the first time through.
    std::vector<std::uint8_t> payload;
    push_step(payload, 20, 0, kOpcodeHeader, {9});
    push_step(payload, 20, 0, kOpcodeCheck, check(105, 1, 2));
    push_step(payload, 20, 1, kOpcodeSet, typed(105, 1));
    push_step(payload, 20, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome first = walk_event(script, 20, state);
    REQUIRE(first.ran);
    REQUIRE(state.variables[105] == 1);

    // Second use: the check passes, the jump skips the set, nothing happens.
    state.variables[105] = 5;
    (void)walk_event(script, 20, state);
    REQUIRE(state.variables[105] == 5);
}

TEST_CASE("a fountain says one thing the first time and another after", "[walk]") {
    // New Sorpigal's own flow: check, set, say, give, goto past the else.
    std::vector<std::uint8_t> payload;
    push_step(payload, 150, 0, kOpcodeHeader, {7});
    push_step(payload, 150, 0, kOpcodeCheck, check(25, 10, 5));
    push_step(payload, 150, 1, kOpcodeSet, typed(25, 10));
    push_step(payload, 150, 2, kOpcodeMessage, {16, 0, 0, 0});
    push_step(payload, 150, 3, kOpcodeGive, typed(205, 3));
    push_step(payload, 150, 4, kOpcodeGoto, {7});
    push_step(payload, 150, 5, kOpcodeMessage, {8, 0, 0, 0});
    push_step(payload, 150, 6, kOpcodeGive, typed(205, 3));
    push_step(payload, 150, 7, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome first = walk_event(script, 150, state);
    REQUIRE(first.said == std::vector<int>{16});
    REQUIRE(state.variables[25] == 10);
    REQUIRE(state.variables[205] == 3);

    const WalkOutcome second = walk_event(script, 150, state);
    REQUIRE(second.said == std::vector<int>{8});
    REQUIRE(state.variables[25] == 10);
    REQUIRE(state.variables[205] == 6);  // the give repeats; the set does not
}

TEST_CASE("a gated exit opens only with its quest bit", "[walk]") {
    // Castle Alamos's own flow: check a bit, jump to the travel step, or fall
    // into the end and go nowhere.
    std::vector<std::uint8_t> travel_args(26, 0);
    travel_args.push_back('D');
    travel_args.push_back('1');
    travel_args.push_back('.');
    travel_args.push_back('B');
    travel_args.push_back('l');
    travel_args.push_back('v');
    travel_args.push_back(0);
    std::vector<std::uint8_t> payload;
    push_step(payload, 47, 0, kOpcodeHeader, {14});
    push_step(payload, 47, 0, kOpcodeCheck, check(kVarQuestBit, 54, 2));
    push_step(payload, 47, 1, kOpcodeEnd, {0});
    push_step(payload, 47, 2, kOpcodeTravel, travel_args);
    const MapScript script = parse(payload);

    WalkState state;
    REQUIRE_FALSE(walk_event(script, 47, state).travel.has_value());

    state.bits.insert(54);
    const WalkOutcome open = walk_event(script, 47, state);
    REQUIRE(open.travel.has_value());
    REQUIRE(open.travel->destination == "D1.Blv");
    REQUIRE(open.acted());
}

TEST_CASE("gives and takes move items and gold", "[walk]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 5, 0, kOpcodeCheck, check(kVarItem, 33, 2));
    push_step(payload, 5, 1, kOpcodeEnd, {0});
    push_step(payload, 5, 2, kOpcodeTake, typed(kVarItem, 33));
    push_step(payload, 5, 3, kOpcodeGive, typed(kVarGold, 1000));
    push_step(payload, 5, 4, kOpcodeGive, typed(kVarItem, 7));
    push_step(payload, 5, 5, kOpcodeTake, typed(kVarGold, 250));
    const MapScript script = parse(payload);

    // Without the item, the check fails and nothing moves.
    WalkState state;
    state.gold = 100;
    REQUIRE_FALSE(walk_event(script, 5, state).acted());
    REQUIRE(state.gold == 100);

    // With it: a quest turn-in — the item goes, gold and a reward come.
    state.items = {33};
    const WalkOutcome turn_in = walk_event(script, 5, state);
    REQUIRE(turn_in.taken == std::vector<int>{33});
    REQUIRE(turn_in.given == std::vector<int>{7});
    REQUIRE(state.gold == 850);
    REQUIRE(state.items == std::vector<int>{7});
}

TEST_CASE("a goto that loops runs out of budget rather than hanging", "[walk]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 9, 0, kOpcodeGoto, {0});
    const MapScript script = parse(payload);
    WalkState state;
    REQUIRE_FALSE(walk_event(script, 9, state).acted());
}

TEST_CASE("an event the map does not define does not run", "[walk]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 1, 0, kOpcodeEnd, {0});
    const MapScript script = parse(payload);
    WalkState state;
    REQUIRE_FALSE(walk_event(script, 99, state).ran);
    REQUIRE(walk_event(script, 1, state).ran);
}
