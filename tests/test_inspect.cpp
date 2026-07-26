// Tests for turning a monster or item row into panel text, and for choosing
// which placed thing the player is looking at.
//
// Hermetic: the rows and the map session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/world/map_session.hpp"
#include "game/inspect.hpp"

using namespace starhaven;
using starhaven::game::inspect;

namespace {

data::MonsterStatsEntry archer() {
    data::MonsterStatsEntry m;
    m.id = 1;
    m.picture = "ArcherA";
    m.name = "Archer";
    m.level = 9;
    m.hit_points = 35;
    m.armor_class = 14;
    m.experience = 171;
    m.attacks[0] = {"Phys", "1D6+1", "Arrow", 0};
    m.resistances[static_cast<std::size_t>(data::Resistance::Fire)] = 10;
    m.resistances[static_cast<std::size_t>(data::Resistance::Cold)] = data::kResistanceImmune;
    return m;
}

// A session holding one monster straight ahead and one item off to the side.
world::MapSession two_things() {
    world::MapSession s;
    s.actors.push_back({"arc1sta", "Archer", 1, {0, 0, -1000}});
    s.objects.push_back({0, "Longsword", 1, {1000, 0, 0}});
    return s;
}

data::MonsterStatsTable monsters_with(const data::MonsterStatsEntry& m) {
    // The table has no public setter, so it is built through its parser from a
    // one-row fixture shaped like MONSTERS.TXT.
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\t" + m.picture + "\t" + m.name + "\t" + std::to_string(m.level) + "\t" +
            std::to_string(m.hit_points) + "\t" + std::to_string(m.armor_class) + "\t" +
            std::to_string(m.experience) +
            "\t0\t0\tN\tShort\tNormal\t4\t140\t90\t0\t0\tPhys\t1D6+1\tArrow\t0\t0\t0\t0\t0\t0"
            "\t10\t0\tImm\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("a monster row becomes readable lines", "[inspect]") {
    const game::Inspected d = game::describe(archer());
    REQUIRE(d.title == "Archer");
    REQUIRE_FALSE(d.empty());

    // The numbers a player wants first.
    REQUIRE(d.lines[0].find("level 9") != std::string::npos);
    REQUIRE(d.lines[0].find("35 hit points") != std::string::npos);
    REQUIRE(d.lines[0].find("armour 14") != std::string::npos);
    REQUIRE(d.lines[1].find("171") != std::string::npos);
}

TEST_CASE("an immune resistance says so rather than printing its sentinel", "[inspect]") {
    // kResistanceImmune is -1; showing that would read as a negative percent.
    const game::Inspected d = game::describe(archer());
    bool found = false;
    for (const auto& line : d.lines) {
        if (line.find("cold immune") != std::string::npos) {
            found = true;
        }
        REQUIRE(line.find("-1") == std::string::npos);
    }
    REQUIRE(found);
}

TEST_CASE("an empty attack slot is not described", "[inspect]") {
    // The second slot is "0" in most rows; printing it would give every
    // monster a phantom attack.
    const game::Inspected d = game::describe(archer());
    int attacks = 0;
    for (const auto& line : d.lines) {
        if (line.rfind("attack:", 0) == 0) {
            ++attacks;
        }
    }
    REQUIRE(attacks == 1);
}

TEST_CASE("an item row becomes readable lines", "[inspect]") {
    data::ItemStatsEntry item;
    item.id = 1;
    item.name = "Longsword";
    item.value = 50;
    item.equip_stat = "Weapon";
    item.skill_group = "Sword";
    item.modifier_1 = "3d3";

    const game::Inspected d = game::describe(item);
    REQUIRE(d.title == "Longsword");
    REQUIRE(d.lines[0] == "Weapon (Sword)");
    REQUIRE(d.lines[1] == "damage 3d3");
    REQUIRE(d.lines[2] == "50 gold");
}

TEST_CASE("looking at nothing inspects nothing", "[inspect]") {
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    // Facing +z, away from both.
    const game::Inspected d = inspect(session, monsters, items, {0, 0, 0}, {0, 0, 1});
    REQUIRE(d.empty());
}

TEST_CASE("looking at a monster inspects it", "[inspect]") {
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, {0, 0, 0}, {0, 0, -1});
    REQUIRE(d.title == "Archer");
}

TEST_CASE("something too far away is not inspected", "[inspect]") {
    world::MapSession session;
    session.actors.push_back({"arc1sta", "Archer", 1, {0, 0, -100000}});
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, {0, 0, 0}, {0, 0, -1});
    REQUIRE(d.empty());
}

TEST_CASE("something off to the side is not inspected", "[inspect]") {
    // The aim cone is about twelve degrees; the item sits at ninety.
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, {0, 0, 0}, {0, 0, -1});
    REQUIRE(d.title != "Longsword");
}

TEST_CASE("a monster with no table row is skipped", "[inspect]") {
    // The map may name a monster id the table does not have; that must not
    // index past the end.
    world::MapSession session;
    session.actors.push_back({"x", "Ghost", 9999, {0, 0, -1000}});
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, {0, 0, 0}, {0, 0, -1});
    REQUIRE(d.empty());
}
