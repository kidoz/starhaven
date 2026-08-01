// Tests for monster movement and for which view of a sprite to draw.
//
// Hermetic: the monster rows and the session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "game/monster_ai.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

constexpr float kPi = 3.14159265f;

// One MONSTERS.TXT row, with the movement columns under test.
data::MonsterStatsTable monsters(const char* move, const char* ai, const char* hostility,
                                 const char* speed) {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += std::string("1\tRatA\tCommon Rat\t2\t6\t4\t24\t0\t0\tN\t") + move + "\t" + ai + "\t" +
            hostility + "\t" + speed +
            "\t90\t0\t0\tPhys\t1D6\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

// A session holding one monster at the origin. Indoor, so nothing samples a
// heightfield that is not there.
world::MapSession one_monster() {
    world::MapSession s;
    s.kind = world::MapKind::Indoor;
    s.actors.push_back({"ratA", "Common Rat", 1, {0, 0, 0}});
    return s;
}

}  // namespace

TEST_CASE("the movement word decides how far a monster strays", "[ai]") {
    REQUIRE(motion_for(monsters("Short", "Normal", "4", "140").entries()[0]).roam == kRoamShort);
    REQUIRE(motion_for(monsters("Med", "Normal", "4", "140").entries()[0]).roam == kRoamMedium);
    REQUIRE(motion_for(monsters("Long", "Normal", "4", "140").entries()[0]).roam == kRoamLong);
    REQUIRE(motion_for(monsters("Free", "Normal", "4", "140").entries()[0]).roam == kRoamFree);
}

TEST_CASE("a monster with no hostility notices nobody", "[ai]") {
    // Nine of the 173 shipped rows have a hostility of 0, every other one 4.
    const auto calm = motion_for(monsters("Long", "Wimp", "0", "140").entries()[0]);
    REQUIRE(calm.notice == 0.0f);
    REQUIRE(calm.roam == kRoamLong);
}

TEST_CASE("only a Wimp runs away", "[ai]") {
    REQUIRE(motion_for(monsters("Med", "Wimp", "4", "140").entries()[0]).flees);
    REQUIRE_FALSE(motion_for(monsters("Med", "Aggress", "4", "140").entries()[0]).flees);
    REQUIRE_FALSE(motion_for(monsters("Med", "Suicidal", "4", "140").entries()[0]).flees);
}

TEST_CASE("the view is the angle between facing and viewer", "[ai]") {
    // Facing east with the eye also to the east: the monster is looking at
    // you, which is view 0. Turn it around and you see its back, view 4.
    REQUIRE(sprite_view(0.0f, 0.0f).index == 0);
    REQUIRE(sprite_view(0.0f, kPi).index == 4);
    REQUIRE(sprite_view(0.0f, kPi / 2.0f).index == 2);
    REQUIRE(sprite_view(kPi, kPi).index == 0);
}

TEST_CASE("the other half of the circle is the same five mirrored", "[ai]") {
    // Five views cover half a turn. The rest is the mirror, which is why a
    // sprite with five views can be seen from any side.
    REQUIRE(sprite_view(0.0f, kPi / 2.0f).index == sprite_view(0.0f, -kPi / 2.0f).index);
    REQUIRE_FALSE(sprite_view(0.0f, kPi / 2.0f).mirror);
    REQUIRE(sprite_view(0.0f, -kPi / 2.0f).mirror);
    REQUIRE_FALSE(sprite_view(0.0f, 0.0f).mirror);
}

TEST_CASE("the view never runs off the end of the five", "[ai]") {
    for (int i = -20; i <= 20; ++i) {
        const auto view = sprite_view(0.0f, static_cast<float>(i) * 0.5f);
        REQUIRE(view.index >= 0);
        REQUIRE(view.index <= 4);
    }
}

TEST_CASE("an aggressive monster closes on the party", "[ai]") {
    auto session = one_monster();
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 7);

    const render::Vec3 party{2000, 0, 0};
    for (int i = 0; i < 60; ++i) {
        mob.update(1.0f / 60.0f, session, party);
    }
    REQUIRE(session.actors[0].position.x > 100.0f);
    REQUIRE(std::abs(session.actors[0].position.z) < 100.0f);
}

TEST_CASE("a wimp goes the other way", "[ai]") {
    auto session = one_monster();
    Mob mob;
    mob.reset(session, monsters("Med", "Wimp", "4", "200"), 7);

    const render::Vec3 party{500, 0, 0};
    for (int i = 0; i < 60; ++i) {
        mob.update(1.0f / 60.0f, session, party);
    }
    REQUIRE(session.actors[0].position.x < 0.0f);
}

TEST_CASE("a monster too far away to notice anyone wanders near home", "[ai]") {
    // Its leash is its movement word. Whatever heading it picks, it comes back.
    auto session = one_monster();
    Mob mob;
    mob.reset(session, monsters("Short", "Normal", "4", "200"), 3);

    const render::Vec3 party{100000, 0, 0};
    float furthest = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        mob.update(1.0f / 60.0f, session, party);
        const auto& p = session.actors[0].position;
        furthest = std::max(furthest, std::sqrt(p.x * p.x + p.z * p.z));
    }
    REQUIRE(furthest > 0.0f);            // it did move
    REQUIRE(furthest < kRoamShort * 2);  // and it did not wander off
}

TEST_CASE("a monster the table does not know stays where it is", "[ai]") {
    auto session = one_monster();
    session.actors[0].monster_id = 0;
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 1);

    for (int i = 0; i < 60; ++i) {
        mob.update(1.0f / 60.0f, session, {1000, 0, 0});
    }
    REQUIRE(session.actors[0].position.x == 0.0f);
    REQUIRE(session.actors[0].position.z == 0.0f);
}

TEST_CASE("a mob that does not match its session does nothing", "[ai]") {
    // The session can be reloaded under it; moving actors that are not the
    // ones it was told about would corrupt the new map.
    auto session = one_monster();
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 1);
    session.actors.push_back({"ratA", "Common Rat", 1, {500, 0, 500}});

    mob.update(1.0f, session, {0, 0, 0});
    REQUIRE(session.actors[0].position.x == 0.0f);
    REQUIRE(session.actors[1].position.x == 500.0f);
}

namespace {

// A session with one wall across the monster's path, and one tree.
world::MapSession walled() {
    world::MapSession s = one_monster();
    const std::array<render::Vec3, 4> wall{render::Vec3{-500, 0, 300}, render::Vec3{500, 0, 300},
                                           render::Vec3{500, 400, 300},
                                           render::Vec3{-500, 400, 300}};
    s.collision.add_polygon(wall, {0, 0, -1});
    return s;
}

}  // namespace

TEST_CASE("a monster does not walk through a wall", "[ai]") {
    auto session = walled();
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 7);

    // The party stands beyond the wall, so the monster walks straight at it.
    const render::Vec3 party{0, 0, 2000};
    for (int i = 0; i < 600; ++i) {
        mob.update(1.0f / 60.0f, session, party);
    }
    REQUIRE(session.actors[0].position.z < 300.0f);
}

TEST_CASE("a monster far from the party is not swept against the level", "[ai]") {
    // Four hundred monsters against three thousand polygons is five
    // milliseconds a frame; the ones you cannot see are left to drift.
    auto session = walled();
    session.actors[0].position = {0, 0, kWallTestRange * 2};
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 7);
    const float before = session.actors[0].position.z;
    mob.update(1.0f / 60.0f, session, {0, 0, 0});
    REQUIRE(session.actors[0].position.z != before);
}

TEST_CASE("monsters do not stand inside each other", "[ai]") {
    world::MapSession session;
    session.kind = world::MapKind::Indoor;
    for (int i = 0; i < 4; ++i) {
        session.actors.push_back({"ratA", "Common Rat", 1, {0, 0, 0}});
    }
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 5);
    mob.update(1.0f / 60.0f, session, {5000, 0, 0});

    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        for (std::size_t j = i + 1; j < session.actors.size(); ++j) {
            const float dx = session.actors[i].position.x - session.actors[j].position.x;
            const float dz = session.actors[i].position.z - session.actors[j].position.z;
            REQUIRE(std::sqrt(dx * dx + dz * dz) > kMonsterSpacing * 0.99f);
        }
    }
}

TEST_CASE("a monster does not stand inside the party", "[ai]") {
    auto session = one_monster();
    session.actors[0].position = {0, 0, 0};
    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 5);

    const render::Vec3 party{0, 0, 0};
    for (int i = 0; i < 120; ++i) {
        mob.update(1.0f / 60.0f, session, party);
    }
    const auto& at = session.actors[0].position;
    REQUIRE(std::sqrt(at.x * at.x + at.z * at.z) >= kPartySpacing * 0.99f);
}

TEST_CASE("a monster does not walk through a tree", "[ai]") {
    // The decoration's own radius is what it takes up; see DecorationTable.
    auto session = one_monster();
    session.kind = world::MapKind::Outdoor;
    session.decorations.push_back({"tree01", {0, 0, 300}, 0, 96});
    constexpr int kTiles = world::OdmTileIndex::kDim * world::OdmTileIndex::kDim;
    session.tile_index.starts.assign(static_cast<std::size_t>(kTiles), 0);
    const int listed = world::OdmTileIndex::tile_y_of(300.0f) * world::OdmTileIndex::kDim +
                       world::OdmTileIndex::tile_x_of(0.0f);
    for (int t = 0; t < kTiles; ++t) {
        session.tile_index.starts[static_cast<std::size_t>(t)] =
            static_cast<std::uint32_t>(session.tile_index.entries.size());
        if (t == listed) {
            session.tile_index.entries.push_back(world::kPidDecoration);  // id 0
        }
        session.tile_index.entries.push_back(0);
    }

    Mob mob;
    mob.reset(session, monsters("Med", "Aggress", "4", "200"), 7);
    for (int i = 0; i < 300; ++i) {
        mob.update(1.0f / 60.0f, session, {0, 0, 1000});
    }
    const auto& at = session.actors[0].position;
    const float dz = at.z - 300.0f;
    REQUIRE(std::sqrt(at.x * at.x + dz * dz) >= 96.0f + kMonsterRadius - 1.0f);
}

TEST_CASE("a monster's attacks are decoded once, at load", "[monsters]") {
    // The 72-byte runtime row holds each attack's damage as bytes — +0x17
    // and +0x1d — so the text is read at parse and never again.
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\tA\tA\t2\t20\t0\t24\t0\t0\tN\tMed\tAggress\t4\t200\t1\t0\t0\tPhys"
            "\t2d6+1\tArrow\t0\tPois\t3d4\t0\t25\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable rows;
    REQUIRE(data::MonsterStatsTable::parse(table, rows) == data::MonsterStatsError::None);
    REQUIRE(rows.entries().size() == 1);
    const auto& m = rows.entries()[0];
    REQUIRE(m.attacks[0].damage_dice.count == 2);
    REQUIRE(m.attacks[0].damage_dice.sides == 6);
    REQUIRE(m.attacks[0].flies);
    REQUIRE(m.attacks[1].damage_dice.count == 3);
    REQUIRE_FALSE(m.attacks[1].flies);
    // And the percentage sits on the attack whose block carries it.
    REQUIRE(m.attacks[0].chance == 0);
    REQUIRE(m.attacks[1].chance == 25);
}
