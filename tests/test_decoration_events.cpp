#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <utility>

#include "core/world/map_session.hpp"
#include "game/save.hpp"
#include "game/script_decorations.hpp"
#include "game/script_message.hpp"

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
        for (const auto byte : step.arguments)
            bytes.push_back(static_cast<std::byte>(byte));
    }
    MapScript result;
    REQUIRE(MapScript::parse(bytes, result) == MapScriptError::None);
    return result;
}
MapSession session() {
    MapSession result;
    result.file_name = "Synthetic.blv";
    result.decorations.resize(2);
    for (auto& decoration : result.decorations) {
        decoration.descriptor_id = 163;
        decoration.flags = 0x104;
    }
    initialize_decoration_events(result);
    return result;
}
std::uint32_t seed_for(int roll) {
    for (std::uint32_t seed = 0; seed < 100000; ++seed) {
        Mm6Random random{seed};
        if (random.next() % 100 == roll)
            return seed;
    }
    FAIL("no seed found for synthetic percentile");
    return 0;
}
}  // namespace

TEST_CASE("decoration event operands are bounded little endian values", "[decoration-events]") {
    const ScriptStep step{1, 0, kOpcodeSetDecorationEvent, {0x78, 0x56, 0x34, 0x12}};
    REQUIRE(parse_decoration_event(step) == 0x12345678U);
    for (std::size_t n = 0; n < 4; ++n) {
        auto short_step = step;
        short_step.arguments.resize(n);
        REQUIRE_FALSE(parse_decoration_event(short_step));
    }
    auto padded = step;
    padded.arguments.push_back(99);
    REQUIRE(parse_decoration_event(padded) == 0x12345678U);
    padded.opcode = kOpcodeGive;
    REQUIRE_FALSE(parse_decoration_event(padded));
}

TEST_CASE("decoration initialization follows observed percentile bands and random consumption",
          "[decoration-events]") {
    for (int roll = 0; roll < 100; ++roll) {
        const auto seed = seed_for(roll);
        for (const auto descriptor :
             std::array<std::uint16_t, 10>{146, 154, 155, 158, 162, 163, 164, 166, 167, 182}) {
            Mm6Random random{seed};
            Mm6Random reference{seed};
            (void)reference.next();
            const auto event = initial_decoration_event(descriptor, random);
            REQUIRE(random.state() == reference.state());
            if (descriptor == 146)
                REQUIRE(event == (roll < 20 ? 37 : 36));
            if (descriptor == 155)
                REQUIRE(event == 43 + roll / 25);
            if (descriptor == 158)
                REQUIRE(event == (roll < 50 ? 29 : 30));
            if (descriptor == 162)
                REQUIRE(event == 35);
            if (descriptor == 163 || descriptor == 164) {
                const std::array boundaries{30, 40, 50, 60, 70, 80, 90};
                int expected = 10;
                for (const int boundary : boundaries)
                    expected += roll >= boundary ? 1 : 0;
                REQUIRE(std::cmp_equal(event, expected));
            }
            if (descriptor == 154 || descriptor == 166) {
                const int start = descriptor == 154 ? 31 : 39;
                REQUIRE(event ==
                        start + (roll >= 40 ? 1 : 0) + (roll >= 70 ? 1 : 0) + (roll >= 90 ? 1 : 0));
            }
            if (descriptor == 167)
                REQUIRE(event ==
                        25 + (roll >= 80 ? 1 : 0) + (roll >= 90 ? 1 : 0) + (roll >= 97 ? 1 : 0));
            if (descriptor == 182)
                REQUIRE(event == (roll < 20 ? 19 : 23));
        }
        Mm6Random random{seed};
        Mm6Random reference{seed};
        (void)reference.next();
        const int expected = roll < 50 ? 47 : 48 + reference.next() % 10;
        REQUIRE(std::cmp_equal(initial_decoration_event(118, random), expected));
        REQUIRE(random.state() == reference.state());
    }
    REQUIRE_FALSE(has_decoration_event(117));
    REQUIRE_FALSE(has_decoration_event(122));
    REQUIRE_FALSE(has_decoration_event(183));
    REQUIRE(has_decoration_event(121));
}

TEST_CASE("initial decoration events are stable bounded and leave explicit events local",
          "[decoration-events]") {
    MapSession map;
    map.file_name = "Synthetic.blv";
    map.decorations.resize(126);
    for (auto& decoration : map.decorations)
        decoration.descriptor_id = 163;
    map.decorations[0].event_id = 7;
    initialize_decoration_events(map);
    REQUIRE_FALSE(map.decorations[0].event_value);
    REQUIRE(map.decorations[124].event_value.has_value());
    REQUIRE_FALSE(map.decorations[125].event_value);
    const auto local = decoration_interaction(map, 0);
    if (!local) {
        FAIL("explicit interaction missing");
        return;
    }
    REQUIRE(local->event == 7);
    REQUIRE_FALSE(local->global);
    REQUIRE_FALSE(decoration_interaction(map, 999));
    const auto initial = map.decorations[1].event_value;
    initialize_decoration_events(map);
    REQUIRE(map.decorations[1].event_value == initial);
    auto fresh = session();
    REQUIRE(fresh.decorations[0].event_value == initial);
}

TEST_CASE("current-decoration changes persist and retain context through a message",
          "[decoration-events]") {
    const auto events = script({
        {410, 0, kOpcodeSetDecorationEvent, {0xa8, 1, 0, 0}},
        {410, 1, kOpcodeShowMessage, {}},
        {410, 2, kOpcodeSetDecorationEvent, {0, 0, 0, 0}},
        {410, 3, kOpcodeEnd, {}},
    });
    auto map = session();
    const auto other = map.decorations[1].event_value;
    DecorationChanges memory;
    WalkState state;
    const auto first = walk_event(events, 410, state, -1, "GLOBAL.EVT", nullptr, nullptr, 0);
    REQUIRE(first.failed_decorations.empty());
    REQUIRE(apply_script_decorations(map, first.decorations, memory) == 1);
    REQUIRE(map.decorations[0].event_value == 24);
    REQUIRE(map.decorations[1].event_value == other);
    const auto next = decoration_interaction(map, 0);
    if (!next || !first.message) {
        FAIL("interaction or continuation missing");
        return;
    }
    REQUIRE(next->event == 424);
    REQUIRE(next->global);
    SaveState saved;
    saved.map_file = map.file_name;
    saved.decorations = memory;
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    auto reopened = session();
    restore_script_decorations(reopened, loaded.decorations);
    REQUIRE(reopened.decorations[0].event_value == 24);
    ScriptMessage message;
    ScriptContinuation pending;
    pending.map = map.file_name;
    pending.event = 410;
    pending.sequence = first.message->resume_at;
    pending.global = true;
    pending.decoration = 0;
    message.show(pending, "Synthetic message");
    message.dismiss();
    const auto continuation = message.take_resume(map.file_name);
    if (!continuation) {
        FAIL("message lost its continuation");
        return;
    }
    const auto last = walk_event(events, continuation->event, state, continuation->sequence,
                                 "GLOBAL.EVT", nullptr, nullptr, continuation->decoration);
    REQUIRE(apply_script_decorations(map, last.decorations, memory) == 1);
    REQUIRE_FALSE(decoration_interaction(map, 0));
    REQUIRE(map.decorations[0].flags == 0x124);
    REQUIRE(map.decorations[1].active());
    saved.decorations = memory;
    REQUIRE(parse_save(save_text(saved), loaded));
    reopened = session();
    restore_script_decorations(reopened, loaded.decorations);
    REQUIRE_FALSE(decoration_interaction(reopened, 0));
    auto elsewhere = session();
    elsewhere.file_name = "Other.blv";
    restore_script_decorations(elsewhere, loaded.decorations);
    REQUIRE(elsewhere.decorations[0].active());
    WalkState no_context;
    const auto missing = walk_event(events, 410, no_context);
    REQUIRE(missing.failed_decorations == std::vector<std::uint8_t>{0});
    REQUIRE(missing.decorations.empty());
}

TEST_CASE("event changes preserve visibility and apply in order with descriptor changes",
          "[decoration-events]") {
    auto map = session();
    DecorationChanges memory;
    for (const std::uint32_t value : {1U, 400U, 655U, 656U, 0xffffffffU}) {
        const std::array change{DecorationChange{0, true, "0", value}};
        REQUIRE(apply_script_decorations(map, change, memory) == 1);
        REQUIRE(map.decorations[0].event_value == static_cast<std::uint8_t>((value + 112U) & 255U));
        REQUIRE(map.decorations[0].active());
    }
    const std::array wrap_to_zero{DecorationChange{0, true, "0", 400}};
    REQUIRE(apply_script_decorations(map, wrap_to_zero, memory) == 1);
    REQUIRE(map.decorations[0].active());
    auto reloaded = session();
    restore_script_decorations(reloaded, memory);
    REQUIRE_FALSE(reloaded.decorations[0].active());
    const std::array changes{
        DecorationChange{0, true, "0", 0},
        DecorationChange{0, true, "0", 424},
    };
    REQUIRE(apply_script_decorations(map, changes, memory) == 2);
    REQUIRE_FALSE(map.decorations[0].active());
    REQUIRE(map.decorations[0].event_value == 24);
    const std::array show{DecorationChange{0, true, "0"}};
    REQUIRE(apply_script_decorations(map, show, memory) == 1);
    REQUIRE(map.decorations[0].active());
    REQUIRE(map.decorations[0].event_value == 24);
    const std::array bad{DecorationChange{99, true, "0", 424}};
    REQUIRE(apply_script_decorations(map, bad, memory) == 0);
}

TEST_CASE("decoration aiming rejects hidden distant and occluded targets", "[decoration-events]") {
    auto map = session();
    map.decorations.resize(1);
    map.decorations[0].position = {0, 0, 100};
    const render::Vec3 eye{0, 32, 0};
    const render::Vec3 forward{0, 0, 1};
    REQUIRE(aimed_decoration(map, eye, forward).has_value());
    REQUIRE_FALSE(aimed_decoration(map, eye, {0, 0, -1}));
    REQUIRE_FALSE(aimed_decoration(map, eye, forward, 50));
    map.decorations[0].flags |= 0x20U;
    REQUIRE_FALSE(aimed_decoration(map, eye, forward));
    map.decorations[0].flags &= static_cast<std::uint16_t>(~0x20U);
    const auto wall = std::to_array<render::Vec3>({
        {-100, -100, 50},
        {100, -100, 50},
        {100, 100, 50},
        {-100, 100, 50},
    });
    map.collision.add_polygon(wall, {0, 0, 1});
    REQUIRE_FALSE(aimed_decoration(map, eye, forward));
}

TEST_CASE("decoration event saves validate appended state and accept old records",
          "[decoration-events]") {
    SaveState saved;
    saved.map_file = "Synthetic.blv";
    saved.decorations["synthetic.blv"][0] = {163, true, 24};
    const auto text = save_text(saved);
    for (const std::string_view value : {"-2", "256", "x", ""}) {
        auto broken = text;
        const auto start = broken.find("decoration\t");
        const auto end = broken.find('\n', start);
        const auto cell = broken.rfind('\t', end);
        broken.replace(cell + 1, end - cell - 1, value);
        SaveState loaded;
        loaded.gold = 999;
        REQUIRE_FALSE(parse_save(broken, loaded));
        REQUIRE(loaded.gold == 999);
    }
    for (const int version : {1, 2, 3, 4}) {
        auto old = text;
        old.replace(0, old.find('\n'), "starhaven-save\t" + std::to_string(version));
        const auto start = old.find("decoration\t");
        const auto end = old.find('\n', start);
        const auto cell = old.rfind('\t', end);
        old.erase(cell, end - cell);
        SaveState loaded;
        REQUIRE(parse_save(old, loaded));
        REQUIRE_FALSE(loaded.decorations.at("synthetic.blv").at(0).event_value);
    }
}
