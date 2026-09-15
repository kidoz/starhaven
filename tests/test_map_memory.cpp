#include "game/map_memory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "game/clock.hpp"
#include "game/combat.hpp"
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
