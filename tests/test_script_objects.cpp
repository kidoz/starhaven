#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <utility>

#include "game/script_coverage.hpp"
#include "game/script_objects.hpp"

using namespace starhaven;

TEST_CASE("object spawn operands preserve signed coordinates, speed and full object ID",
          "[script-objects]") {
    world::ScriptStep step{
        7,
        2,
        world::kOpcodeSpawnObjects,
        {
            0x34, 0x12, 0xab, 0x89, 0,    0,    0,    0x80, 0xff, 0xff, 0xff,
            0x7f, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 255,  128,
        },
    };
    const auto request = world::parse_object_spawn(step);
    if (!request) {
        FAIL("complete spawn operands did not parse");
        return;
    }
    REQUIRE(request->object_id == 0x89ab1234U);
    REQUIRE(request->x == std::numeric_limits<std::int32_t>::min());
    REQUIRE(request->y == std::numeric_limits<std::int32_t>::max());
    REQUIRE(request->z == -1);
    REQUIRE(request->speed == -2);
    REQUIRE(std::cmp_equal(request->count, 255));
    REQUIRE(request->scatter);
    for (std::size_t length = 0; length < 22; ++length) {
        auto truncated = step;
        truncated.arguments.resize(length);
        REQUIRE_FALSE(world::parse_object_spawn(truncated));
    }
    step.arguments[20] = 0;
    step.arguments[21] = 0;
    step.arguments.push_back(255);  // trailing bytes are not another operand
    const auto zero = world::parse_object_spawn(step);
    if (!zero) {
        FAIL("zero-count spawn with trailing bytes did not parse");
        return;
    }
    REQUIRE(std::cmp_equal(zero->count, 0));
    REQUIRE_FALSE(zero->scatter);
    step.opcode = world::kOpcodeTravel;
    REQUIRE_FALSE(world::parse_object_spawn(step));
}

TEST_CASE("object spawn joins use low sixteen bits for descriptors and full IDs for loot",
          "[script-objects]") {
    std::array<world::ObjectDescriptor, 3> objects;
    objects[1].object_id = 7;
    objects[2].object_id = 7;  // first match wins
    std::array<data::ItemStatsEntry, 3> items;
    items[0].id = 0;
    items[0].sprite_index = 0;
    items[1].id = 1;
    items[1].sprite_index = 7;
    items[2].id = 2;
    items[2].sprite_index = 7;  // reverse lookup chooses the first item
    world::ObjectSpawnRequest request;
    request.object_id = 7;
    const auto loot = game::resolve_object_spawn(request, objects, items);
    if (!loot) {
        FAIL("known descriptor did not resolve");
        return;
    }
    REQUIRE(std::cmp_equal(loot->descriptor_index, 1));
    REQUIRE(loot->item_id == 1);
    request.object_id = 0x10007;
    const auto effect = game::resolve_object_spawn(request, objects, items);
    if (!effect) {
        FAIL("low-word descriptor did not resolve");
        return;
    }
    REQUIRE(std::cmp_equal(effect->descriptor_index, 1));
    REQUIRE(effect->item_id == 0);
    request.object_id = 8;
    REQUIRE_FALSE(game::resolve_object_spawn(request, objects, items));
    REQUIRE_FALSE(game::resolve_object_spawn(request, {}, items));
    request.object_id = 7;
    const auto no_item = game::resolve_object_spawn(request, objects, {});
    if (!no_item) {
        FAIL("an absent item match must not reject a descriptor");
        return;
    }
    REQUIRE(no_item->item_id == 0);
    items[1].sprite_index = 263;  // compiled ITEMS stores this column as a byte
    const auto compiled_byte = game::resolve_object_spawn(request, objects, items);
    if (!compiled_byte) {
        FAIL("compiled item sprite byte did not resolve");
        return;
    }
    REQUIRE(compiled_byte->item_id == 1);
}

TEST_CASE("object descriptor indices must fit the runtime word", "[script-objects]") {
    std::vector<world::ObjectDescriptor> objects(65537);
    objects.back().object_id = 1;
    world::ObjectSpawnRequest request;
    request.object_id = 1;
    REQUIRE_FALSE(game::resolve_object_spawn(request, objects, {}));
    objects[65535].object_id = 1;
    const auto last = game::resolve_object_spawn(request, objects, {});
    if (!last) {
        FAIL("largest representable descriptor index did not resolve");
        return;
    }
    REQUIRE(std::cmp_equal(last->descriptor_index, 65535));
    request.object_id = 0;
    const auto unused = game::resolve_object_spawn(request, objects, {});
    if (!unused) {
        FAIL("a matched zero descriptor must remain visible to the audit");
        return;
    }
    REQUIRE(std::cmp_equal(unused->descriptor_index, 0));
}

TEST_CASE("decoding object spawns does not claim runtime dispatch support", "[script-objects]") {
    const auto info = game::script_opcode_coverage(world::kOpcodeSpawnObjects);
    REQUIRE(info.name == "SpawnObjects");
    REQUIRE(info.dispatch == game::ScriptDispatch::Unsupported);
}
