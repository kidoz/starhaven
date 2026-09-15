#include "game/map_memory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "game/clock.hpp"
#include "game/combat.hpp"
#include "game/party_event.hpp"
#include "game/save.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

struct World {
    world::MapSession session;
    Battle battle;
    std::set<int> chests;
    std::vector<world::MonsterAnimation> shown{world::MonsterAnimation::Stand};

    World() {
        session.kind = world::MapKind::Indoor;
        session.refill_days = 7;
        session.actors.push_back({"synthetic", "Synthetic actor", 1, {}});
        world::MapDoor door;
        door.id = 12;
        door.attributes = 1;
        door.open = true;
        door.progress = 1.0f;
        session.doors.push_back(door);
        data::TextTable text;
        REQUIRE(data::TextTable::parse_body(
                    "#\tPicture\tName\tLVL\tHP\n1\tsynthetic\tSynthetic actor\t1\t10\n", text) ==
                data::TextTableError::None);
        data::MonsterStatsTable monsters;
        REQUIRE(data::MonsterStatsTable::parse(text, monsters) == data::MonsterStatsError::None);
        battle.reset(session, monsters, 1);
        REQUIRE(battle.alive(0));
    }

    MapMemoryResult restore(const MapMemory& memory, MapMemoryUse use, std::int64_t day) {
        return restore_map_memory(memory, use, day, session, battle, chests, shown);
    }
};

}  // namespace

TEST_CASE("loading an earlier saved map preserves its cleared enemies despite the live date",
          "[map-memory]") {
    SaveState saved;
    saved.map_file = "Synthetic.blv";
    saved.minutes = std::int64_t{2} * kMinutesPerDay;
    saved.remembered.push_back({saved.map_file, 2, {3}, {}, {0}});
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    REQUIRE(loaded.remembered.size() == 1);
    const auto& record = loaded.remembered.front();
    const MapMemory memory{record.opened_chests, record.open_doors, record.dead, record.day};
    World world;
    const GameClock previous_clock{std::int64_t{20} * kMinutesPerDay};
    REQUIRE(world.restore(memory, MapMemoryUse::SavedSnapshot, previous_clock.day()) ==
            MapMemoryResult::Restored);
    REQUIRE_FALSE(world.battle.alive(0));
    REQUIRE(world.shown[0] == world::MonsterAnimation::Death);
    REQUIRE(world.chests == std::set<int>{3});
    REQUIRE_FALSE(world.session.doors[0].open);
    REQUIRE(world.session.doors[0].progress == 0.0f);
    REQUIRE(previous_clock.day() == 20);
}

TEST_CASE("revisit expiry remains distinct from restoring a saved snapshot", "[map-memory]") {
    const MapMemory memory{{3}, {12}, {0}, 2};
    for (const auto day : {1, 8, 9, 20}) {
        CAPTURE(day);
        World world;
        const auto result = world.restore(memory, MapMemoryUse::Revisit, day);
        const bool expired = day >= 9;
        REQUIRE(result == (expired ? MapMemoryResult::Expired : MapMemoryResult::Restored));
        REQUIRE(world.battle.alive(0) == expired);
        REQUIRE(world.chests.contains(3) == !expired);
        REQUIRE(world.session.doors[0].open);
    }
    World permanent;
    permanent.session.refill_days = 0;
    REQUIRE(permanent.restore(memory, MapMemoryUse::Revisit, 10000) == MapMemoryResult::Restored);
    REQUIRE_FALSE(permanent.battle.alive(0));
}

TEST_CASE("remembered closed doors override the initially open map default", "[map-memory]") {
    World world;
    const MapMemory memory{{}, {}, {}, 2};
    REQUIRE(world.restore(memory, MapMemoryUse::Revisit, 3) == MapMemoryResult::Restored);
    REQUIRE_FALSE(world.session.doors[0].open);
    REQUIRE(world.session.doors[0].progress == 0.0f);
    REQUIRE(world.battle.alive(0));
}

TEST_CASE("extreme remembered dates have a bounded refill comparison", "[map-memory]") {
    MapMemory memory{{3}, {}, {0}, std::numeric_limits<std::int64_t>::min()};
    World ancient;
    REQUIRE(ancient.restore(memory, MapMemoryUse::Revisit, 20) == MapMemoryResult::Expired);
    REQUIRE(ancient.battle.alive(0));
    World snapshot;
    REQUIRE(snapshot.restore(memory, MapMemoryUse::SavedSnapshot, 20) == MapMemoryResult::Restored);
    REQUIRE_FALSE(snapshot.battle.alive(0));
    memory.remembered_day = std::numeric_limits<std::int64_t>::max();
    World future;
    REQUIRE(future.restore(memory, MapMemoryUse::Revisit, 20) == MapMemoryResult::Restored);
}

TEST_CASE("legacy map spellings share a snapshot and active state replaces every alias",
          "[map-memory]") {
    SaveState saved;
    saved.map_file = "SYNTHETIC.BLV";
    saved.remembered = {
        {"Synthetic.blv", 10, {1}, {12}, {}},
        {"synthetic.BLV", 2, {3}, {}, {0}},
        {"Synthetic.odm", 1, {9}, {}, {}},
    };
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    const auto memories = load_map_memories(loaded.remembered);
    REQUIRE(memories.size() == 2);
    const auto& indoors = memories.at(map_memory_key(loaded.map_file));
    REQUIRE(indoors.remembered_day == 2);
    REQUIRE(indoors.opened_chests == std::set<int>{3});
    REQUIRE(indoors.open_doors.empty());
    REQUIRE(indoors.dead == std::vector<std::size_t>{0});
    REQUIRE(memories.at(map_memory_key("SYNTHETIC.ODM")).opened_chests == std::set<int>{9});
    const auto records = save_map_memories(memories, "Synthetic.BlV", {{4}, {12}, {}, 3});
    REQUIRE(records.size() == 2);
    REQUIRE(records.back().file == "synthetic.blv");
    const auto reloaded = load_map_memories(records);
    REQUIRE(reloaded.at("synthetic.blv").opened_chests == std::set<int>{4});
    REQUIRE(reloaded.at("synthetic.blv").dead.empty());
    REQUIRE(reloaded.at("synthetic.odm").opened_chests == std::set<int>{9});
}

TEST_CASE("rewards survive travel return and save reload with mixed case map names",
          "[map-memory][journey]") {
    World origin;
    origin.session.file_name = "Synthetic.blv";
    const std::vector<data::GeneratedItem> contents{{2, 1, 3, 5, 27, false}, {1, 0, 0, 0, 0, true}};
    ScriptItemState rewards;
    std::array<Pack, 4> packs;
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(packs[i].add(9, kPackWidth, kPackHeight));
    }
    REQUIRE(claim_chest_items(7, contents, origin.chests, rewards));
    REQUIRE(deliver_script_item(rewards.pending.front(), kPackWidth, kPackHeight, packs));
    rewards.pending.erase(rewards.pending.begin());
    REQUIRE_FALSE(deliver_script_item(rewards.pending.front(), 1, 1, packs));

    // A real EVT give enters through the same walk/settlement seam as main.
    std::vector<std::byte> bytes(48, std::byte{0});
    const std::vector<std::uint8_t> payload{
        9, 1, 0, 0, world::kOpcodeGive, world::kVarGoldFound, 100, 0, 0, 0,
        4, 1, 0, 1, world::kOpcodeEnd,
    };
    for (const auto byte : payload) {
        bytes.push_back(static_cast<std::byte>(byte));
    }
    world::MapScript events;
    REQUIRE(world::MapScript::parse(bytes, events) == world::MapScriptError::None);
    WalkState state;
    int purse = 25;
    state.gold = purse;
    ScriptContinuation request{origin.session.file_name, 1, -1, false, false, {}};
    const std::array<Character, 4> party{};
    const auto outcome = walk_party_event(events, request, state, party, 0);
    REQUIRE(settle_script_gold(state, outcome, 10, purse) == 110);
    REQUIRE(purse == 135);
    origin.battle.kill(0);
    origin.session.doors[0].open = false;

    MapMemories memories;
    memories[map_memory_key(origin.session.file_name)] =
        capture_map_memory(origin.session, origin.battle, origin.chests, 2);
    World destination;
    destination.session.file_name = "Other.blv";
    REQUIRE(destination.chests.empty());
    REQUIRE(destination.battle.alive(0));
    memories[map_memory_key(destination.session.file_name)] =
        capture_map_memory(destination.session, destination.battle, destination.chests, 3);
    World returned;
    returned.session.file_name = "SYNTHETIC.BLV";
    REQUIRE(returned.restore(memories.at(map_memory_key(returned.session.file_name)),
                             MapMemoryUse::Revisit, 4) == MapMemoryResult::Restored);
    REQUIRE_FALSE(returned.battle.alive(0));
    REQUIRE_FALSE(returned.session.doors[0].open);
    REQUIRE_FALSE(claim_chest_items(7, contents, returned.chests, rewards));
    REQUIRE(rewards.pending == std::vector<data::GeneratedItem>{contents[1]});

    SaveState saved;
    saved.map_file = returned.session.file_name;
    saved.minutes = std::int64_t{4} * kMinutesPerDay;
    saved.gold = purse;
    saved.script_items = rewards;
    saved.opened_chests.assign(returned.chests.begin(), returned.chests.end());
    saved.remembered = save_map_memories(
        memories, saved.map_file,
        capture_map_memory(returned.session, returned.battle, returned.chests, 4));
    for (std::size_t i = 0; i < packs.size(); ++i) {
        saved.packs[i] = packs[i].items();
    }
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    REQUIRE(loaded.gold == 135);
    REQUIRE(loaded.remembered.size() == 2);
    const auto restored = load_map_memories(loaded.remembered);
    World reloaded;
    REQUIRE(reloaded.restore(restored.at(map_memory_key("sYnThEtIc.BlV")),
                             MapMemoryUse::SavedSnapshot, 50) == MapMemoryResult::Restored);
    REQUIRE_FALSE(reloaded.battle.alive(0));
    REQUIRE_FALSE(reloaded.session.doors[0].open);
    REQUIRE(reloaded.chests == std::set<int>{7});
    REQUIRE_FALSE(claim_chest_items(7, contents, reloaded.chests, loaded.script_items));
    REQUIRE(loaded.script_items.pending == std::vector<data::GeneratedItem>{contents[1]});
    std::array<Pack, 4> restored_packs;
    for (std::size_t i = 0; i < restored_packs.size(); ++i) {
        for (const auto& item : loaded.packs[i]) {
            REQUIRE(restored_packs[i].place(item));
        }
    }
    const auto& carried = restored_packs[3].items().front();
    REQUIRE(carried.item_id == contents[0].item_id);
    REQUIRE(carried.standard_bonus == contents[0].standard_bonus);
    REQUIRE(carried.standard_strength == contents[0].standard_bonus_strength);
    REQUIRE(carried.special_bonus == contents[0].special_bonus);
    REQUIRE(carried.charges == contents[0].charges);
    REQUIRE(carried.identified == contents[0].identified);
    REQUIRE_FALSE(deliver_script_item(loaded.script_items.pending.front(), 1, 1, restored_packs));
    restored_packs[0].clear();
    REQUIRE(deliver_script_item(loaded.script_items.pending.front(), 1, 1, restored_packs));
    loaded.script_items.pending.clear();
    REQUIRE_FALSE(claim_chest_items(7, contents, reloaded.chests, loaded.script_items));
    REQUIRE(loaded.script_items.pending.empty());
    REQUIRE(restored_packs[0].items().size() == 1);
    REQUIRE(restored_packs[0].items().front().item_id == contents[1].item_id);
}
