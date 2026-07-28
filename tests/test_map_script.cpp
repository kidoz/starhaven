// Tests for a map's event script and its strings.
//
// Hermetic: the fixtures are built by hand from the format described in
// docs/formats/map-events.md.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "core/world/map_script.hpp"

using namespace starhaven::world;

namespace {

// The 48-byte container the archive wraps these in. A zero unpacked size means
// the bytes are stored as they are, which is what these fixtures use.
std::vector<std::byte> wrap(const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> out(48, std::byte{0});
    for (const std::uint8_t b : payload) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

// One record: size, then id, sequence, opcode and arguments.
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

}  // namespace

TEST_CASE("a script is a run of size-prefixed steps", "[script]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 1, 0, 4, {7});
    push_step(payload, 1, 1, 5, {0x18});
    push_step(payload, 2, 0, 15, {1, 1});
    const auto entry = wrap(payload);

    MapScript script;
    REQUIRE(MapScript::parse(entry, script) == MapScriptError::None);
    REQUIRE(script.size() == 3);
    REQUIRE(script.steps()[0].event_id == 1);
    REQUIRE(script.steps()[0].opcode == 4);
    REQUIRE(script.steps()[0].arguments == std::vector<std::uint8_t>{7});
    REQUIRE(script.steps()[1].sequence == 1);
    REQUIRE(script.steps()[2].event_id == 2);
    REQUIRE(script.steps()[2].arguments.size() == 2);
}

TEST_CASE("an event's steps are asked for together", "[script]") {
    std::vector<std::uint8_t> payload;
    push_step(payload, 1, 0, 4, {7});
    push_step(payload, 2, 0, 4, {8});
    push_step(payload, 2, 1, 5, {9});
    const auto entry = wrap(payload);

    MapScript script;
    REQUIRE(MapScript::parse(entry, script) == MapScriptError::None);
    REQUIRE(script.event(2).size() == 2);
    REQUIRE(script.event(2)[1].opcode == 5);
    REQUIRE(script.event(1).size() == 1);
    REQUIRE(script.event(99).empty());
    REQUIRE(script.defines(1));
    REQUIRE_FALSE(script.defines(99));
}

TEST_CASE("a record that runs past the end is refused", "[script]") {
    // The walk is exact on all 83 shipped scripts, so a size that does not fit
    // means the file is not one.
    std::vector<std::uint8_t> payload{9, 1, 0, 0, 4};  // says nine bytes, has four
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::BadRecord);
    REQUIRE(script.size() == 0);

    // And a size too small to hold an id, a sequence and an opcode.
    REQUIRE(MapScript::parse(wrap({2, 1, 0}), script) == MapScriptError::BadRecord);
}

TEST_CASE("a container too short to hold a header is refused", "[script]") {
    MapScript script;
    REQUIRE(MapScript::parse(std::vector<std::byte>(12, std::byte{0}), script) ==
            MapScriptError::BadContainer);
}

TEST_CASE("strings are what lies between the terminators", "[script]") {
    const std::vector<std::uint8_t> payload{' ', 0,   'E', 'x', 'i', 't', 0,
                                            'C', 'h', 'e', 's', 't', 0};
    MapStrings strings;
    REQUIRE(MapStrings::parse(wrap(payload), strings) == MapScriptError::None);
    REQUIRE(strings.size() == 3);
    REQUIRE(strings.at(0) == " ");
    REQUIRE(strings.at(1) == "Exit");
    REQUIRE(strings.at(2) == "Chest");
    REQUIRE(strings.at(99).empty());
}

TEST_CASE("a string left unterminated is still a string", "[script]") {
    MapStrings strings;
    REQUIRE(MapStrings::parse(wrap({'D', 'o', 'o', 'r'}), strings) == MapScriptError::None);
    REQUIRE(strings.size() == 1);
    REQUIRE(strings.at(0) == "Door");
}

TEST_CASE("an event's message and name are asked for by opcode", "[script]") {
    // Opcodes 29, 30 and 35 carry a string index in their first argument;
    // see docs/formats/map-events.md.
    std::vector<std::uint8_t> payload;
    push_step(payload, 7, 0, 4, {1});
    push_step(payload, 7, 1, kOpcodeName, {3});
    push_step(payload, 7, 2, kOpcodeMessage, {5, 0, 0, 0});
    const auto entry = wrap(payload);

    MapScript script;
    REQUIRE(MapScript::parse(entry, script) == MapScriptError::None);
    REQUIRE(script.string_of(7, kOpcodeName) == 3);
    REQUIRE(script.string_of(7, kOpcodeMessage) == 5);
    REQUIRE(script.string_of(7, kOpcodeLongMessage) == -1);
    REQUIRE(script.string_of(99, kOpcodeName) == -1);
}

TEST_CASE("only the three known opcodes name a string", "[script]") {
    REQUIRE(names_a_string(kOpcodeMessage));
    REQUIRE(names_a_string(kOpcodeLongMessage));
    REQUIRE(names_a_string(kOpcodeName));
    // Opcode 4 is the commonest of the 90 and is not one: its argument leaves
    // the string range 522 times.
    REQUIRE_FALSE(names_a_string(4));
    REQUIRE_FALSE(names_a_string(1));
}

TEST_CASE("the title opcode names a string too", "[script]") {
    // Opcode 5 points at the map's own display name on 53 of the 54 shipped
    // uses that resolve; see docs/formats/map-events.md.
    REQUIRE(names_a_string(kOpcodeTitle));

    std::vector<std::uint8_t> payload;
    push_step(payload, 1, 0, kOpcodeTitle, {4});
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);
    REQUIRE(script.string_of(1, kOpcodeTitle) == 4);
}

TEST_CASE("an entry event names the establishment it opens", "[script]") {
    // Opcode 2's argument is a 2DEvents row id, four bytes wide: the ids run
    // past 255, which is why reading only the first byte finds nothing.
    std::vector<std::uint8_t> payload;
    push_step(payload, 12, 0, kOpcodeEnter, {0x89, 0x00, 0x00, 0x00});
    push_step(payload, 13, 0, kOpcodeMessage, {1, 0, 0, 0});
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);
    REQUIRE(script.building_of(12) == 137);
    REQUIRE(script.building_of(13) == 0);
    REQUIRE(script.building_of(99) == 0);
}

TEST_CASE("a chest event names which chest", "[script]") {
    // The argument indexes the event file's fixed 20-slot array; the largest
    // value across all 65 shipped scripts is 19.
    std::vector<std::uint8_t> payload;
    push_step(payload, 3, 0, kOpcodeChest, {0});
    push_step(payload, 4, 0, kOpcodeChest, {19});
    push_step(payload, 5, 0, kOpcodeMessage, {1, 0, 0, 0});
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);
    // Chest zero is a chest: the absence of one cannot be reported as zero.
    REQUIRE(script.chest_of(3) == 0);
    REQUIRE(script.chest_of(4) == 19);
    REQUIRE(script.chest_of(5) == -1);
    REQUIRE(script.chest_of(99) == -1);
}

TEST_CASE("a travel event says where the party goes", "[script]") {
    // Four little-endian i32s — X, Y, Z, facing — ten bytes not yet decoded,
    // then the NUL-terminated destination map at byte 26.
    std::vector<std::uint8_t> args{
        0x95, 0x1e, 0x00, 0x00,                      // x = 7829
        0xfb, 0xe3, 0xff, 0xff,                      // y = -7173
        0xe0, 0x00, 0x00, 0x00,                      // z = 224
        0x38, 0x02, 0x00, 0x00,                      // facing = 568
        0,    0,    0,    0,    0, 0, 0, 0, 0, 8,    // undecoded
        'O',  'u',  't',  'D',  '1', '.', 'O', 'd', 'm', 0};
    std::vector<std::uint8_t> payload;
    push_step(payload, 7, 0, kOpcodeTravel, args);
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);

    const auto travel = script.travel_of(7);
    REQUIRE(travel);
    REQUIRE(travel->x == 7829);
    REQUIRE(travel->y == -7173);
    REQUIRE(travel->z == 224);
    REQUIRE(travel->facing == 568);
    REQUIRE(travel->destination == "OutD1.Odm");
    REQUIRE_FALSE(script.travel_of(99));
}

TEST_CASE("a travel event that names no map stays on this one", "[script]") {
    // A destination of "0" is a teleporter: 126 of the 232 shipped uses.
    std::vector<std::uint8_t> args(26, 0);
    args.push_back('0');
    args.push_back(0);
    std::vector<std::uint8_t> payload;
    push_step(payload, 3, 0, kOpcodeTravel, args);
    // A degenerate use with no room for a destination is not a travel at all:
    // 13 shipped uses carry zero or one byte.
    push_step(payload, 4, 0, kOpcodeTravel, {1});
    MapScript script;
    REQUIRE(MapScript::parse(wrap(payload), script) == MapScriptError::None);

    const auto travel = script.travel_of(3);
    REQUIRE(travel);
    REQUIRE(travel->destination.empty());
    REQUIRE_FALSE(script.travel_of(4));
}
