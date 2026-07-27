// Tests for turning a map's spawn points into monsters.
//
// Hermetic: the tables are synthesized from the formats described in
// docs/formats/text-tables.md and docs/formats/odm-tile-index.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/world/monster_spawn.hpp"

using namespace starhaven;
using namespace starhaven::world;

namespace {

// Three rows of a MONSTERS.TXT triple, plus a unique with no triple of its
// own — the shipped table prefixes those with a "z".
data::MonsterStatsTable monsters() {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    const auto row = [&](const char* id, const char* picture, const char* name) {
        body += std::string(id) + "\t" + picture + "\t" + name +
                "\t2\t6\t4\t24\t0\t0\tN\tShort\tNormal\t4\t140\t90\t0\t0\tPhys\t1D6\t0\t0\t0\t0"
                "\t0\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    };
    row("1", "RatA", "Common Rat");
    row("2", "RatB", "Large Rat");
    row("3", "RatC", "Giant Rat");
    row("4", "zDemonqueen", "Demon Queen");
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

data::MapEncounter slot(const char* picture, const char* name, const char* count) {
    data::MapEncounter e;
    e.picture = picture;
    e.monster = name;
    e.count = count;
    return e;
}

}  // namespace

TEST_CASE("an appearance range is read as written", "[spawn]") {
    REQUIRE(parse_spawn_count(" 2-4").low == 2);
    REQUIRE(parse_spawn_count(" 2-4").high == 4);
    REQUIRE(parse_spawn_count("3").low == 3);
    REQUIRE(parse_spawn_count("3").high == 3);
}

TEST_CASE("a range that says nothing spawns nothing", "[spawn]") {
    // Better than a guess: an empty or malformed cell means the slot is not
    // used, and inventing a count would put monsters where the table did not.
    REQUIRE(parse_spawn_count("").empty());
    REQUIRE(parse_spawn_count("0").empty());
    REQUIRE(parse_spawn_count("4-2").empty());
    REQUIRE(parse_spawn_count("some").empty());
}

TEST_CASE("a spawn index selects one of the map's three encounter slots", "[spawn]") {
    REQUIRE(encounter_slot_of(1) == 0);
    REQUIRE(encounter_slot_of(2) == 1);
    REQUIRE(encounter_slot_of(3) == 2);
    REQUIRE(encounter_slot_of(0) == -1);
}

TEST_CASE("the indices past three wrap onto the same slots", "[spawn]") {
    // 40 of the 848 shipped spawn points carry 6, 9, 10, 11 or 12. Wrapping is
    // what lands each of them on a slot its own map fills; the reading is
    // inferred, not established.
    REQUIRE(encounter_slot_of(6) == 2);
    REQUIRE(encounter_slot_of(9) == 2);
    REQUIRE(encounter_slot_of(10) == 0);
    REQUIRE(encounter_slot_of(11) == 1);
    REQUIRE(encounter_slot_of(12) == 2);
}

TEST_CASE("an encounter slot names the first of a triple", "[spawn]") {
    // The slot's picture is "Rat", and the table's rows are RatA, RatB, RatC.
    const auto table = monsters();
    REQUIRE(encounter_monster_id(table, slot("Rat", "Common Rat", "2-4")) == 1);
}

TEST_CASE("a unique has no triple to be the first of", "[spawn]") {
    // The Demon Queen is the one encounter slot of the 138 whose picture has
    // no A variant; the table carries it with a "z" in front instead.
    const auto table = monsters();
    REQUIRE(encounter_monster_id(table, slot("DemonQueen", "Devil Queen", "1")) == 4);
}

TEST_CASE("a slot naming nothing resolves to nothing", "[spawn]") {
    const auto table = monsters();
    REQUIRE(encounter_monster_id(table, slot("", "", "")) == 0);
    REQUIRE(encounter_monster_id(table, slot("0", "0", "0")) == 0);
    REQUIRE(encounter_monster_id(table, slot("Wyvern", "Wyvern", "2-4")) == 0);
}

TEST_CASE("a group stands around its spawn point, not on it", "[spawn]") {
    const auto [x0, y0] = spawn_offset(0, 1);
    REQUIRE(x0 == 0.0f);
    REQUIRE(y0 == 0.0f);

    const auto [x1, y1] = spawn_offset(0, 4);
    const auto [x2, y2] = spawn_offset(2, 4);
    REQUIRE(x1 != x2);
    REQUIRE(std::abs(x1 - x2) > kSpawnGroupSpread);
    REQUIRE(std::abs(y1) < 1.0f);
    REQUIRE(std::abs(y2) < 1.0f);
}
