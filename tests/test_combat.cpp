// Tests for hitting things and being hit.
//
// Hermetic: the monster and item rows and the session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/combat.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

// One MONSTERS.TXT row, with the combat columns under test.
data::MonsterStatsTable monsters(const char* hp, const char* ac, const char* damage,
                                 const char* cold = "0") {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += std::string("1\tRatA\tCommon Rat\t2\t") + hp + "\t" + ac +
            "\t24\t0\t0\tN\tMed\tAggress\t4\t200\t100\t0\t0\tCold\t" + damage +
            "\t0\t100\t0\t0\t0\t0\t0\t0\t0\t" + cold + "\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

data::ItemStatsTable items() {
    std::string body =
        "\r\nItem #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
        "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    body += "0\t\t\t0\t\t\t0\t0\t0\t0\t\t0\t0\t0\t0\t\r\n";
    body += "1\tlsword1\tLongsword\t50\tWeapon\tSword\t3d3\t0\t8\t1\tLongsword\t1\t4\t0\t0\t\r\n";
    body += "2\tring1\tRing\t100\tRing\t\t0\t0\t8\t1\tRing\t2\t4\t0\t0\t\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::ItemStatsTable out;
    REQUIRE(data::ItemStatsTable::parse(table, out) == data::ItemStatsError::None);
    return out;
}

world::MapSession with_monster(render::Vec3 at) {
    world::MapSession s;
    s.kind = world::MapKind::Indoor;
    s.actors.push_back({"ratA", "Common Rat", 1, at});
    return s;
}

Character fighter() {
    Character c;
    c.name = "Aaron";
    c.hit_points = 30;
    c.max_hit_points = 30;
    c.attributes.fill(15);
    return c;
}

}  // namespace

TEST_CASE("a monster starts at the hit points its row gives", "[combat]") {
    const auto session = with_monster({0, 0, 0});
    Battle battle;
    battle.reset(session, monsters("6", "4", "1d4"), 1);
    REQUIRE(battle.size() == 1);
    REQUIRE(battle.alive(0));
    REQUIRE_FALSE(battle.alive(99));
}

TEST_CASE("enough blows kill", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    std::string last;
    for (int i = 0; i < 40 && battle.alive(0); ++i) {
        last = battle.strike(0, who, pack, session, table, items());
        REQUIRE_FALSE(last.empty());
    }
    REQUIRE_FALSE(battle.alive(0));
    REQUIRE(last.find("kills it") != std::string::npos);
}

TEST_CASE("something already dead cannot be struck again", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("1", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 5);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items());
    }
    REQUIRE(battle.strike(0, who, pack, session, table, items()).empty());
}

TEST_CASE("a character swings the weapon they carry", "[combat]") {
    // A longsword is 3d3 and a fist is 1d3, so the sword cannot roll a 1 and
    // the fist cannot roll a 9.
    const auto table = items();
    Pack empty;
    REQUIRE(weapon_of(empty, table).sides == kBareHandSides);
    REQUIRE(weapon_of(empty, table).count == 1);

    Pack armed;
    REQUIRE(armed.add(1, 1, 1));
    REQUIRE(weapon_of(armed, table).count == 3);
    REQUIRE(weapon_of(armed, table).sides == 3);

    // A ring is not a weapon, so it is not swung.
    Pack jewellery;
    REQUIRE(jewellery.add(2, 1, 1));
    REQUIRE(weapon_of(jewellery, table).count == 1);
}

TEST_CASE("a monster in reach hits somebody standing", "[combat]") {
    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    bool swung = false;
    int hurt = 0;
    // Half its blows miss, so this waits for one to land rather than for one
    // to be thrown.
    for (int i = 0; i < 200 && hurt == 0; ++i) {
        // Not `swung = swung || update(...)`: that stops swinging as soon as
        // the first blow is thrown, and the first one is usually a miss.
        const std::string blow = battle.update(0.5f, session, table, party, {0, 0, 0});
        swung = swung || !blow.empty();
        hurt = 0;
        for (const auto& who : party) {
            hurt += who.hit_points < who.max_hit_points ? 1 : 0;
        }
    }
    REQUIRE(swung);
    REQUIRE(hurt >= 1);
}

TEST_CASE("a monster out of reach hits nobody", "[combat]") {
    auto session = with_monster({0, 0, kMeleeRange * 4});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (int i = 0; i < 100; ++i) {
        REQUIRE(battle.update(0.5f, session, table, party, {0, 0, 0}).empty());
    }
    REQUIRE(party[0].hit_points == party[0].max_hit_points);
}

TEST_CASE("immunity stops a blow entirely", "[combat]") {
    REQUIRE(after_resistance(10, data::kResistanceImmune) == 0);
    REQUIRE(after_resistance(10, 0) == 10);
    REQUIRE(after_resistance(10, 50) == 5);
    // A resisted blow still stings: rounding must not reach zero by itself.
    REQUIRE(after_resistance(1, 90) == 1);
}

TEST_CASE("an attack is answered by the resistance of its own type", "[combat]") {
    const auto table = monsters("6", "0", "1d4", "Imm");
    const auto& monster = table.entries()[0];
    REQUIRE(resistance_to(monster, "Cold") == data::kResistanceImmune);
    REQUIRE(resistance_to(monster, "Fire") == 0);
    // Physical damage has no resistance column of its own; armour answers it.
    REQUIRE(resistance_to(monster, "Phys") == 0);
    REQUIRE(resistance_to(monster, "Ener") == 0);
}

TEST_CASE("the party aims at what it is looking at", "[combat]") {
    world::MapSession session;
    session.kind = world::MapKind::Indoor;
    session.actors.push_back({"ratA", "Common Rat", 1, {0, 0, -200}});
    session.actors.push_back({"ratA", "Common Rat", 1, {200, 0, 0}});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 1);

    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, -1}, kPartyReach) == 0);
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {1, 0, 0}, kPartyReach) == 1);
    // Nothing behind you, and nothing beyond arm's length.
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, 1}, kPartyReach) == kNoActor);
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, -1}, 100.0f) == kNoActor);
}
