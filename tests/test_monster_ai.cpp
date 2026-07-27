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
