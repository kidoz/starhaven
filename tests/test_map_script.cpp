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
