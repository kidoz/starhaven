// Synthetic scripts, decoration descriptors and map sessions only.
#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>

#include "core/world/map_session.hpp"
#include "game/save.hpp"
#include "game/script_decorations.hpp"
#include "game/script_walk.hpp"

using namespace starhaven;
using namespace starhaven::game;
using namespace starhaven::world;

namespace {

void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value,
         std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU);
    }
}

std::vector<std::byte> compressed(const std::vector<std::uint8_t>& raw) {
    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> packed(size);
    REQUIRE(compress(packed.data(), &size, raw.data(), static_cast<uLong>(raw.size())) == Z_OK);
    std::vector<std::byte> bytes(48, std::byte{0});
    for (std::size_t i = 0; i < size; ++i) {
        bytes.push_back(static_cast<std::byte>(packed[i]));
    }
    return bytes;
}

DecorationTable table() {
    std::vector<std::uint8_t> raw(4 + 3 * 80);
    put(raw, 0, 3);
    const std::array<std::string, 3> names{"none", "Lamp", "Spent"};
    const std::array<std::uint16_t, 3> descriptor_flags{2, 0, 1};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto at = 4 + i * 80;
        std::copy(names[i].begin(), names[i].end(), raw.begin() + static_cast<std::ptrdiff_t>(at));
        put(raw, at + 0x42, 120, 2);  // height, distinct from radius
        put(raw, at + 0x44, i == 1 ? 17 : 0, 2);
        put(raw, at + 0x4a, descriptor_flags[i], 2);
        put(raw, at + 0x4c, i == 1 ? 77 : 0, 2);
    }
    DecorationTable result;
    REQUIRE(DecorationTable::parse(compressed(raw), result) == DecorationTableError::None);
    return result;
}

MapSession session(std::string filename = "Test.blv") {
    MapSession result;
    result.file_name = std::move(filename);
    result.decoration_types = table();
    result.decorations.push_back({"Lamp", {10, 20, 30}, 77, 17, 0x104, 0, 1});
    result.decorations.push_back({"Lamp", {40, 50, 60}, 77, 17, 0, 0, 1});
    return result;
}

ScriptStep change(std::uint32_t index, std::uint8_t visible, std::string_view name) {
    ScriptStep result{1, 0, kOpcodeSetDecoration, std::vector<std::uint8_t>(5)};
    put(result.arguments, 0, index);
    result.arguments[4] = visible;
    result.arguments.insert(result.arguments.end(), name.begin(), name.end());
    result.arguments.push_back(0);
    return result;
}

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

TEST_CASE("decoration changes validate their bounded layout and signed index", "[decorations]") {
    const auto valid = change(0x123456, 255, "Lamp");
    const auto decoded = parse_decoration_change(valid);
    if (!decoded) {
        FAIL("complete decoration change did not decode");
        return;
    }
    REQUIRE(decoded->index == 0x123456);
    REQUIRE(decoded->visible);
    REQUIRE(decoded->name == "Lamp");
    for (std::size_t n = 0; n < valid.arguments.size(); ++n) {
        auto truncated = valid;
        truncated.arguments.resize(n);
        REQUIRE_FALSE(parse_decoration_change(truncated));
    }
    REQUIRE_FALSE(
        parse_decoration_change(change(std::numeric_limits<std::uint32_t>::max(), 1, "0")));
    REQUIRE(parse_decoration_change(change(0, 0, "")));  // empty name resolves to descriptor zero
    auto other = valid;
    other.opcode = kOpcodeRetexture;
    REQUIRE_FALSE(parse_decoration_change(other));
}

TEST_CASE("a quest event changes its decoration and continues into quest and NPC state",
          "[decorations]") {
    const auto event = script({
        change(0, 0, "sPeNt"),
        {1, 1, kOpcodeTake, {kVarQuestBit, 91, 0, 0, 0}},
        {1, 2, kOpcodeSetTopic, {7, 0, 0, 0, 1, 88, 0, 0, 0}},
        {1, 3, kOpcodeEnd, {}},
    });
    WalkState state;
    state.bits.insert(91);
    const auto outcome = walk_event(event, 1, state);
    REQUIRE(outcome.acted());
    REQUIRE(outcome.unsupported.empty());
    REQUIRE(outcome.decorations.size() == 1);
    REQUIRE_FALSE(state.bits.contains(91));
    REQUIRE(state.resolved_quests.contains(91));
    REQUIRE(state.npc_topics.at({7, 1}) == 88);
    auto map = session();
    DecorationChanges memory;
    REQUIRE(apply_script_decorations(map, outcome.decorations, memory) == 1);
    REQUIRE(map.decorations[0].name == "Spent");
    REQUIRE(map.decorations[0].descriptor_id == 2);
    REQUIRE(map.decorations[0].sound_id == 0);
    REQUIRE(map.decorations[0].radius == 0);
    REQUIRE_FALSE(map.decorations[0].visible());
    REQUIRE_FALSE(map.decorations[0].blocks_movement());
    REQUIRE(map.decorations[0].flags == 0x124);  // unrelated flags survive
    REQUIRE(map.decorations[0].position.x == 10);
    REQUIRE(map.decorations[1].name == "Lamp");
    REQUIRE(map.decorations[1].visible());
}

TEST_CASE("descriptor retention, unknown names and out-of-range targets follow the original",
          "[decorations]") {
    auto map = session();
    DecorationChanges memory;
    const std::array changes{DecorationChange{0, false, "0"}, DecorationChange{0, true, "0"}};
    REQUIRE(apply_script_decorations(map, changes, memory) == 2);
    REQUIRE(map.decorations[0].name == "Lamp");
    REQUIRE(map.decorations[0].flags == 0x104);
    REQUIRE(map.decorations[0].sound_id == 77);
    REQUIRE(map.decorations[0].blocks_movement());
    const std::array invalid{
        DecorationChange{2, false, "Spent"},
        DecorationChange{std::numeric_limits<std::uint32_t>::max(), false, "Spent"},
    };
    const auto saved = memory;
    REQUIRE(apply_script_decorations(map, invalid, memory) == 0);
    REQUIRE(memory == saved);
    const std::array unknown{DecorationChange{0, true, "not-in-table"}};
    REQUIRE(apply_script_decorations(map, unknown, memory) == 1);
    REQUIRE(map.decorations[0].descriptor_id == 0);
    REQUIRE(map.decorations[0].active());
    REQUIRE_FALSE(map.decorations[0].visible());  // descriptor zero has no-draw in this fixture
    REQUIRE(map.decorations[0].sound_id == 0);
    REQUIRE_FALSE(map.decorations[0].blocks_movement());
}

TEST_CASE("decoration results survive map changes and saves without crossing map namespaces",
          "[decorations]") {
    auto first = session("First.BLV");
    auto second = session("Second.odm");
    DecorationChanges memory;
    const std::array off{DecorationChange{0, false, "Spent"}};
    const std::array on{DecorationChange{0, true, "Lamp"}};
    REQUIRE(apply_script_decorations(first, off, memory) == 1);
    REQUIRE(apply_script_decorations(second, on, memory) == 1);
    SaveState saved;
    saved.map_file = first.file_name;
    saved.decorations = memory;
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    REQUIRE(loaded.decorations == memory);
    auto reopened = session("FIRST.blv");
    restore_script_decorations(reopened, loaded.decorations);
    REQUIRE(reopened.decorations[0].name == "Spent");
    REQUIRE_FALSE(reopened.decorations[0].visible());
    restore_script_decorations(second, loaded.decorations);
    REQUIRE(second.decorations[0].name == "Lamp");
    REQUIRE(second.decorations[0].visible());
    memory.erase("first.blv");  // the map refill policy discards these overrides
    reopened = session("First.blv");
    restore_script_decorations(reopened, memory);
    REQUIRE(reopened.decorations[0].visible());
    REQUIRE(reopened.decorations[0].name == "Lamp");
}

TEST_CASE(
    "hidden decorations leave the nearby index while descriptor flags govern drawing and blocking",
    "[decorations]") {
    auto map = session();
    map.kind = MapKind::Outdoor;
    constexpr auto kDimension = static_cast<std::size_t>(OdmTileIndex::kDim);
    map.tile_index.starts.assign(kDimension * kDimension, 2);
    const auto tile = static_cast<std::size_t>(OdmTileIndex::tile_y_of(0)) * kDimension +
                      static_cast<std::size_t>(OdmTileIndex::tile_x_of(0));
    std::fill_n(map.tile_index.starts.begin(), tile + 1, 0);
    map.tile_index.entries = {kPidDecoration, 0};
    REQUIRE(map.decorations_near(0, 0) == std::vector<std::size_t>{0});
    map.decorations[0].flags |= 0x20U;
    REQUIRE(map.decorations_near(0, 0).empty());
    REQUIRE_FALSE(map.decorations[0].visible());
    REQUIRE_FALSE(map.decorations[0].blocks_movement());
    map.decorations[0].flags &= static_cast<std::uint16_t>(~0x20U);
    map.decorations[0].descriptor_flags = 1;
    REQUIRE(map.decorations[0].visible());
    REQUIRE_FALSE(map.decorations[0].blocks_movement());
    REQUIRE(map.decoration_types.at(1)->height == 120);
    REQUIRE(map.decoration_types.at(1)->radius == 17);
}

TEST_CASE("decoration art follows its numeric frame reference when names differ", "[decorations]") {
    auto map = session();
    std::vector<std::uint8_t> raw(8 + kSpriteFrameSize + 2);
    put(raw, 0, 1);
    put(raw, 4, 1);
    const std::string animation = "flame";
    std::ranges::copy(animation, raw.begin() + 8);
    const std::string sprite = "flame_a";
    std::ranges::copy(sprite, raw.begin() + 8 + kSpriteFrameNameSize);
    put(raw, 8 + 40, 65536);  // scale
    put(raw, 8 + 44, 4);      // group head
    REQUIRE(SpriteFrameTable::parse(compressed(raw), map.sprite_frames) == SpriteFrameError::None);
    REQUIRE(map.decorations[0].name == "Lamp");
    REQUIRE(map.decoration_animation(map.decorations[0]) == "flame");
    map.decorations[0].descriptor_id = 60000;
    REQUIRE(map.decoration_animation(map.decorations[0]) == "Lamp");
}
