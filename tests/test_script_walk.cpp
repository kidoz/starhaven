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
    // Type 205 is the autonote chronicle now; the give lands in its set.
    REQUIRE(state.autonotes.contains(3));

    const WalkOutcome second = walk_event(script, 150, state);
    REQUIRE(second.said == std::vector<int>{8});
    REQUIRE(state.variables[25] == 10);
    REQUIRE(state.autonotes.contains(3));
    REQUIRE(state.autonotes.contains(3));  // the give repeats; the set does not
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

TEST_CASE("a switch's event names the face it repaints", "[walk]") {
    // Opcode 11: a u32 face and a NUL-terminated texture name. All 215 named
    // shipped uses are BITMAPS.LOD entries — the lever drawn thrown.
    std::vector<std::uint8_t> args{0xdb, 0x07, 0x00, 0x00, 't', '1', 's', 'w', 'd', 'u', 0};
    std::vector<std::uint8_t> payload;
    push_step(payload, 20, 0, kOpcodeRetexture, args);
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome outcome = walk_event(script, 20, state);
    REQUIRE(outcome.retextures.size() == 1);
    REQUIRE(outcome.retextures[0].first == 2011);
    REQUIRE(outcome.retextures[0].second == "t1swdu");
    REQUIRE(outcome.acted());
}

TEST_CASE("a lever's event names the doors it throws", "[walk]") {
    // Opcode 15: a door id and a state — 0 shuts, 1 opens. D01's paired
    // levers open two doors and shut the other two.
    std::vector<std::uint8_t> payload;
    push_step(payload, 20, 0, kOpcodeDoor, {55, 1});
    push_step(payload, 20, 1, kOpcodeDoor, {57, 0});
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome outcome = walk_event(script, 20, state);
    REQUIRE(outcome.doors == std::vector<std::pair<int, int>>{{55, 1}, {57, 0}});
    REQUIRE(outcome.acted());
}

TEST_CASE("a quest event rewrites what an NPC offers and where they stand", "[walk]") {
    // Opcode 39 sets one of the NPC's three topic slots — Andover's letter
    // event moves his first topic to 2 — and opcode 40 moves the NPC, zero
    // meaning away.
    std::vector<std::uint8_t> payload;
    push_step(payload, 1, 0, kOpcodeSetTopic, {1, 0, 0, 0, 0, 2, 0, 0, 0});
    push_step(payload, 1, 1, kOpcodeMoveNpc, {43, 0, 0, 0, 230, 1, 0, 0});
    push_step(payload, 1, 2, kOpcodeMoveNpc, {44, 0, 0, 0, 0, 0, 0, 0});
    const MapScript script = parse(payload);

    WalkState state;
    (void)walk_event(script, 1, state);
    REQUIRE(state.npc_topics.at({1, 0}) == 2);
    REQUIRE(state.npc_places.at(43) == 486);
    REQUIRE(state.npc_places.at(44) == 0);
}

TEST_CASE("a trap's event names what it summons and where", "[walk]") {
    // Opcode 19: the map's encounter slot, the A/B/C variant, a count, and a
    // point — slot within the map's own filled slots on 272 of 272 uses.
    std::vector<std::uint8_t> args{1, 2, 3};
    for (const std::int32_t v : {-7522, 14848, -240}) {
        for (int i = 0; i < 4; ++i) {
            args.push_back(static_cast<std::uint8_t>((static_cast<std::uint32_t>(v) >> (8 * i)) &
                                                     0xFF));
        }
    }
    std::vector<std::uint8_t> payload;
    push_step(payload, 26, 0, kOpcodeSummon, args);
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome outcome = walk_event(script, 26, state);
    REQUIRE(outcome.summons.size() == 1);
    REQUIRE(outcome.summons[0].slot == 1);
    REQUIRE(outcome.summons[0].variant == 2);
    REQUIRE(outcome.summons[0].count == 3);
    REQUIRE(outcome.summons[0].x == -7522);
    REQUIRE(outcome.summons[0].y == 14848);
    REQUIRE(outcome.summons[0].z == -240);
    REQUIRE(outcome.acted());
}

TEST_CASE("a trap's event puts a sprite in the air", "[walk]") {
    // Opcode 21: an animation, a byte, and two points; the second all zeros
    // when the record states no target.
    std::vector<std::uint8_t> args{6, 0, 3};
    for (const std::int32_t v : {2496, 4864, 360, 0, 0, 0}) {
        for (int i = 0; i < 4; ++i) {
            args.push_back(static_cast<std::uint8_t>((static_cast<std::uint32_t>(v) >> (8 * i)) &
                                                     0xFF));
        }
    }
    std::vector<std::uint8_t> payload;
    push_step(payload, 19, 0, kOpcodeLaunch, args);
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome outcome = walk_event(script, 19, state);
    REQUIRE(outcome.launches.size() == 1);
    REQUIRE(outcome.launches[0].animation == 6);
    REQUIRE(outcome.launches[0].from_x == 2496);
    REQUIRE(outcome.launches[0].from_y == 4864);
    REQUIRE(outcome.launches[0].from_z == 360);
    REQUIRE(outcome.launches[0].aimless());
    REQUIRE(outcome.acted());
}

TEST_CASE("a question stops the walk and an answer resumes it", "[walk]") {
    // Opcode 26, the shipped shape: message, ask, the miss branch, then the
    // match branch at the step the record names.
    std::vector<std::uint8_t> payload;
    push_step(payload, 9, 0, kOpcodeLongMessage, {5, 0, 0, 0});
    push_step(payload, 9, 1, kOpcodeAsk, {6, 0, 0, 0, 7, 0, 0, 0, 8, 0, 0, 0, 4});
    push_step(payload, 9, 2, kOpcodeMessage, {9, 0, 0, 0});  // "Wrong!"
    push_step(payload, 9, 3, kOpcodeEnd, {0});
    push_step(payload, 9, 4, kOpcodeMessage, {10, 0, 0, 0});  // "You may pass"
    const MapScript script = parse(payload);

    WalkState state;
    const WalkOutcome asked = walk_event(script, 9, state);
    REQUIRE(asked.ask.has_value());
    REQUIRE(asked.ask->prompt == 6);
    REQUIRE(asked.ask->answer_a == 7);
    REQUIRE(asked.ask->answer_b == 8);
    REQUIRE(asked.ask->step_on_match == 4);
    REQUIRE(asked.ask->step_on_miss == 2);
    REQUIRE(asked.said == std::vector<int>{5});
    REQUIRE(asked.acted());

    const WalkOutcome matched = walk_event(script, 9, state, asked.ask->step_on_match);
    REQUIRE(matched.said == std::vector<int>{10});
    const WalkOutcome missed = walk_event(script, 9, state, asked.ask->step_on_miss);
    REQUIRE(missed.said == std::vector<int>{9});
}

TEST_CASE("a random jump rolls one of its listed steps", "[walk]") {
    // Opcode 25: six slots, zero-padded. Whatever the dice say, the walk
    // must land on a listed step's message or, through a zero, fall through.
    std::vector<std::uint8_t> payload;
    push_step(payload, 3, 0, kOpcodeRandomJump, {2, 4, 0, 0, 0, 0});
    push_step(payload, 3, 1, kOpcodeMessage, {1, 0, 0, 0});  // fall-through
    push_step(payload, 3, 2, kOpcodeMessage, {2, 0, 0, 0});
    push_step(payload, 3, 3, kOpcodeEnd, {0});
    push_step(payload, 3, 4, kOpcodeMessage, {4, 0, 0, 0});
    const MapScript script = parse(payload);

    std::set<int> seen;
    WalkState state;
    for (int roll = 0; roll < 64; ++roll) {
        const WalkOutcome outcome = walk_event(script, 3, state);
        REQUIRE(outcome.said.size() >= 1);
        seen.insert(outcome.said.front());
    }
    // All three doors of this little roulette get walked through eventually.
    REQUIRE(seen == std::set<int>{1, 2, 4});
}

TEST_CASE("a quest pays experience and food alongside its gold", "[walk]") {
    // GLOBAL event 4's shape: message, then gives of gold (21),
    // experience (13) and food (23).
    std::vector<std::uint8_t> payload;
    push_step(payload, 4, 0, kOpcodeMessage, {6, 0, 0, 0});
    push_step(payload, 4, 1, kOpcodeGive, typed(21, 2000));
    push_step(payload, 4, 2, kOpcodeGive, typed(13, 2000));
    push_step(payload, 4, 3, kOpcodeGive, typed(23, 5));
    push_step(payload, 4, 4, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    state.food = 2;
    const auto outcome = starhaven::game::walk_event(script, 4, state);
    REQUIRE(outcome.ran);
    REQUIRE(state.gold == 2000);
    REQUIRE(state.experience == 2000);
    REQUIRE(state.food == 7);

    // A take cannot push a currency below nothing.
    std::vector<std::uint8_t> drain;
    push_step(drain, 5, 0, kOpcodeTake, typed(23, 100));
    push_step(drain, 5, 1, kOpcodeEnd, {0});
    const MapScript drained = parse(drain);
    (void)starhaven::game::walk_event(drained, 5, state);
    REQUIRE(state.food == 0);
}

TEST_CASE("a cure and a barrel land in the outcome, not the variables", "[walk]") {
    // "Cures 10 hit points", "+2 Luck permanent", "+5 Fire resistance":
    // types 3, 38 and 46 — Luck's stored value is 38, the last of the run
    // that begins at 32.
    std::vector<std::uint8_t> payload;
    push_step(payload, 9, 0, kOpcodeGive, typed(3, 10));
    push_step(payload, 9, 1, kOpcodeGive, typed(5, 4));
    push_step(payload, 9, 2, kOpcodeGive, typed(38, 2));
    push_step(payload, 9, 3, kOpcodeGive, typed(46, 5));
    push_step(payload, 9, 4, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 9, state);
    REQUIRE(outcome.healed_hp == 10);
    REQUIRE(outcome.healed_sp == 4);
    REQUIRE(outcome.stat_gains[6] == 2);    // Luck is variable 38, the seventh
    REQUIRE(outcome.resist_gains[0] == 5);  // Fire is the first
    REQUIRE(outcome.acted());
    REQUIRE(state.variables.find(38) == state.variables.end());
}

TEST_CASE("a quest sets an award and a later check wears it", "[walk]") {
    // Goblinwatch's shape: set award 53, then an event gated on it.
    std::vector<std::uint8_t> payload;
    push_step(payload, 4, 0, kOpcodeSet, typed(12, 53));
    push_step(payload, 4, 1, kOpcodeEnd, {0});
    push_step(payload, 7, 0, kOpcodeCheck, check(12, 53, 2));
    push_step(payload, 7, 1, kOpcodeEnd, {0});
    push_step(payload, 7, 2, kOpcodeMessage, {5, 0, 0, 0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    (void)starhaven::game::walk_event(script, 7, state);
    REQUIRE_FALSE(state.awards.contains(53));
    (void)starhaven::game::walk_event(script, 4, state);
    REQUIRE(state.awards.contains(53));
    const auto outcome = starhaven::game::walk_event(script, 7, state);
    REQUIRE(outcome.said == std::vector<int>{5});
}

TEST_CASE("found gold fills the purse and reports itself apart", "[walk]") {
    // D05's shape: a dig pays type 22, a quest pays type 21.
    std::vector<std::uint8_t> payload;
    push_step(payload, 3, 0, kOpcodeGive, typed(22, 400));
    push_step(payload, 3, 1, kOpcodeGive, typed(21, 100));
    push_step(payload, 3, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 3, state);
    REQUIRE(state.gold == 500);
    REQUIRE(outcome.gold_found == 400);
    REQUIRE(outcome.acted());
}

TEST_CASE("a harm step surfaces who, what and how much", "[walk]") {
    // D05's cave-in shape: the whole party, physical, twenty.
    std::vector<std::uint8_t> payload;
    push_step(payload, 6, 0, kOpcodeHarm, {5, 0, 20, 0, 0, 0});
    push_step(payload, 6, 1, kOpcodeHarm, {2, 4, 30, 0, 0, 0});
    push_step(payload, 6, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 6, state);
    REQUIRE(outcome.harms.size() == 2);
    REQUIRE(outcome.harms[0].target == 5);
    REQUIRE(outcome.harms[0].element == 0);
    REQUIRE(outcome.harms[0].amount == 20);
    REQUIRE(outcome.harms[1].target == 2);
    REQUIRE(outcome.harms[1].element == 4);  // poison, the resistance order
    REQUIRE(outcome.harms[1].amount == 30);
    REQUIRE(outcome.acted());
}

TEST_CASE("a title step names the place and the noun step the object", "[walk]") {
    std::vector<std::uint8_t> payload;
    // Opcode 5 with a one-byte string index (the place), opcode 35 likewise
    // (the interactable noun), then end.
    push_step(payload, 7, 0, kOpcodeTitle, {210});
    push_step(payload, 7, 1, kOpcodeName, {14});
    push_step(payload, 7, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 7, state);
    REQUIRE(outcome.title == 210);
    REQUIRE(outcome.name == 14);
    REQUIRE(outcome.said.empty());  // heads the dialogue, not part of the body
    REQUIRE(outcome.acted());
}

TEST_CASE("a switch disables and re-enables another event", "[walk]") {
    // Opcode 32: [event u32 LE][on/off u8]. Disable event 33, then re-enable it.
    std::vector<std::uint8_t> payload;
    push_step(payload, 14, 0, kOpcodeSwitch, {33, 0, 0, 0, 0});  // disable 33
    push_step(payload, 14, 1, kOpcodeEnd, {0});
    const MapScript script = parse(payload);

    starhaven::game::WalkState state;
    auto outcome = starhaven::game::walk_event(script, 14, state);
    REQUIRE(state.disabled_events.contains(33));
    REQUIRE(!outcome.acted());  // a switch is not observable dialogue

    // A second event re-enables 33.
    std::vector<std::uint8_t> payload2;
    push_step(payload2, 15, 0, kOpcodeSwitch, {33, 0, 0, 0, 1});  // enable 33
    push_step(payload2, 15, 1, kOpcodeEnd, {0});
    const MapScript script2 = parse(payload2);
    starhaven::game::walk_event(script2, 15, state);
    REQUIRE(!state.disabled_events.contains(33));
}

TEST_CASE("both attribute runs reach the same seven gains", "[script]") {
    // 32..38 are the stored values and 25..31 the modifiers beside them, on
    // the offsets the property setter's own case bodies compute; a script
    // raising either raises the attribute.
    std::vector<std::uint8_t> payload;
    push_step(payload, 4, 0, kOpcodeGive, typed(32, 3));   // Might, stored
    push_step(payload, 4, 1, kOpcodeGive, typed(25, 2));   // Might, modifier
    push_step(payload, 4, 2, kOpcodeGive, typed(31, 6));   // Luck, modifier
    push_step(payload, 4, 3, kOpcodeEnd, {0});
    const MapScript script = parse(payload);
    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 4, state);
    REQUIRE(outcome.stat_gains[0] == 5);
    REQUIRE(outcome.stat_gains[6] == 6);
    REQUIRE(outcome.stat_gains[3] == 0);
}

TEST_CASE("the property runs the setter's cases name", "[script]") {
    // Each of these is fixed by the offset the case body computes, not by a
    // fit: id 56 lands on `+0x60`, id 87 on `+0x1380`, id 225 on `+0x1410`.
    REQUIRE(kVarStatModFirst == 25);
    REQUIRE(kVarStatFirst == 32);
    REQUIRE(kVarStatModAlias == 39);
    REQUIRE(kVarSkillFirst == 56);
    REQUIRE(kVarConditionFirst == 87);
    REQUIRE(kVarSkillPool == 225);
    // The two attribute runs do not overlap, and the alias sits past both.
    REQUIRE(kVarStatModFirst + 7 == kVarStatFirst);
    REQUIRE(kVarStatFirst + 7 == kVarStatModAlias);
    // Thirty-one skills and seventeen conditions leave no gap between them.
    REQUIRE(kVarSkillFirst + 31 == kVarConditionFirst);
    REQUIRE(kVarConditionFirst + 17 == 104);
}

TEST_CASE("either name for a modifier raises the same attribute", "[script]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 5, 0, kOpcodeGive, typed(25, 3));  // Might, modifier
    push_step(payload, 5, 1, kOpcodeGive, typed(39, 4));  // Might again
    push_step(payload, 5, 2, kOpcodeGive, typed(45, 1));  // Luck, the alias
    push_step(payload, 5, 3, kOpcodeEnd, {0});
    const MapScript script = parse(payload);
    starhaven::game::WalkState state;
    const auto outcome = starhaven::game::walk_event(script, 5, state);
    REQUIRE(outcome.stat_gains[0] == 7);
    REQUIRE(outcome.stat_gains[6] == 1);
}

TEST_CASE("the level is variable nine", "[script]") {
    // Both routines share one selector and index it with id - 1; id 9 lands
    // on the body that writes the word at +0x32, which the max-hit-point
    // getter reads as the level.
    REQUIRE(kVarLevel == 9);
    REQUIRE(kVarLevelModifier == kVarLevel + 1);
    REQUIRE(kVarBirthWord == kVarLevel + 2);
    REQUIRE(kVarClass == 2);
    // Full restores are their own ids, one past the ones that add.
    REQUIRE(kVarHitPointsFull == kVarHitPoints + 1);
    REQUIRE(kVarSpellPointsFull == kVarSpellPoints + 1);
    // The resistances are five bases then five modifiers, and the skills
    // begin right after them.
    REQUIRE(kVarResistModFirst == kVarResistFirst + 5);
    REQUIRE(kVarResistModFirst + 5 == kVarSkillFirst);
    REQUIRE(kVarClearConditions == kVarConditionFirst + 17);
}

TEST_CASE("six clocks belong to the scripts alone", "[script]") {
    REQUIRE(kVarTimerFirst == 216);
    REQUIRE(kScriptTimerCount == 6);
    // They sit between the party globals and the autonote, and do not run
    // into either.
    REQUIRE(kVarTimerFirst + kScriptTimerCount == 222);
    REQUIRE(kVarAutonote < kVarTimerFirst);

    std::vector<std::uint8_t> payload;
    push_step(payload, 7, 0, kOpcodeGive, typed(kVarTimerFirst, 40));
    push_step(payload, 7, 1, kOpcodeGive, typed(kVarTimerFirst + 5, 90));
    push_step(payload, 7, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);
    starhaven::game::WalkState state;
    (void)starhaven::game::walk_event(script, 7, state);
    REQUIRE(state.timers[0] == 40);
    REQUIRE(state.timers[5] == 90);
    REQUIRE(state.timers[3] == 0);
    // Nothing leaks into the plain variable store.
    REQUIRE(state.variables.find(kVarTimerFirst) == state.variables.end());
}

TEST_CASE("the last three property ids before the clocks", "[script]") {
    REQUIRE(kVarCharacterBit == 213);
    REQUIRE(kVarHirelingMark == 214);
    REQUIRE(kVarReputation == 215);
    REQUIRE(kVarReputation + 1 == kVarTimerFirst);
    // The bit array sits between the readied spell and the stored terms.
    // Sixty-four bytes is five hundred and twelve bits — far more than the
    // ninety-nine spells the obvious guess would want, which is why the name
    // is not taken.
    REQUIRE(kCharacterBitArray == 0x1530);
    REQUIRE(kCharacterBitArrayBytes == 0x40);
    REQUIRE(kCharacterBitArray + kCharacterBitArrayBytes == 0x1570);
    REQUIRE(kCharacterBitArrayBytes * 8 > 99 * 4);
}

TEST_CASE("the character's bits belong to the scripts alone", "[script]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 8, 0, kOpcodeGive, typed(kVarCharacterBit, 40));
    push_step(payload, 8, 1, kOpcodeGive, typed(kVarCharacterBit, 511));
    push_step(payload, 8, 2, kOpcodeEnd, {0});
    const MapScript script = parse(payload);
    starhaven::game::WalkState state;
    (void)starhaven::game::walk_event(script, 8, state);
    REQUIRE(state.character_bits.contains(40));
    REQUIRE(state.character_bits.contains(511));
    REQUIRE_FALSE(state.character_bits.contains(41));
    // They are their own store, not the party's quest bits and not variables.
    REQUIRE_FALSE(state.bits.contains(40));
    REQUIRE(state.variables.find(kVarCharacterBit) == state.variables.end());
    // The array is big enough for every bit a script could name in a byte
    // pair, which is why the ninety-nine-spell reading was refused.
    REQUIRE(kCharacterBitArrayBytes * 8 == 512);
}
