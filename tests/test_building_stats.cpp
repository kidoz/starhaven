// Tests for the town establishment table.
//
// Hermetic: the fixture is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/building_stats.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

// Two rows of merged headings, the real header, then data. Column positions
// match the shipped table.
std::string body() {
    std::string s;
    s += "2D Events by Type\t\t\r\n";
    s += "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tSchedules\t\r\n";
    s += "#\t#\tType\tMap\tPicture\tName\tProprieter Name\tTitle\tPicture\tState\tRep\tPer\tVal"
         "\tA\tB\tC\tNotes:\t\tOpen\tClosed\tPic\tMap\tRestrictions\tText\r\n";
    s += "1\t1\tWeapon Shop\tE3\t2\tThe Knife Shoppe\tCaine\tBlacksmith\t0\t0\t0\t0\t1.5"
         "\tL1 Weap\tL2 Dagger\t2\tnotes\t\t6\t18\t0\t0\t0\t\r\n";
    s += "2\t2\tTavern\te3\t3\tThe Laughing Monk\tPeter\tBarkeep\t0\t0\t0\t0\t1"
         "\tale\tmead\t0\t\t\t9\t23\t0\t0\t0\t\r\n";
    s += "3\t1\tTemple\tD2,C3,B1\t1\tThe Wandering Shrine\tAnon\tPriest\t0\t0\t0\t0\t1"
         "\t0\t0\t0\t\t\t8\t20\t0\t0\t0\t\r\n";
    return s;
}

BuildingStatsTable parsed() {
    TextTable table;
    REQUIRE(TextTable::parse_body(body(), table) == TextTableError::None);
    BuildingStatsTable out;
    REQUIRE(BuildingStatsTable::parse(table, out) == BuildingStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("establishments parse into typed rows", "[buildings]") {
    const auto buildings = parsed();
    REQUIRE(buildings.size() == 3);

    const auto& shop = buildings.entries()[0];
    REQUIRE(shop.id == 1);
    REQUIRE(shop.type == "Weapon Shop");
    REQUIRE(shop.name == "The Knife Shoppe");
    REQUIRE(shop.proprietor == "Caine");
    REQUIRE(shop.title == "Blacksmith");
    REQUIRE(shop.opens == 6);
    REQUIRE(shop.closes == 18);
    REQUIRE(shop.stock_a == "L1 Weap");
}

TEST_CASE("the map cell is matched without regard to case", "[buildings]") {
    // The table writes "E3" on one row and "e3" on another.
    const auto buildings = parsed();
    const auto here = buildings.on_map("E3");
    REQUIRE(here.size() == 2);
    REQUIRE(here[0]->name == "The Knife Shoppe");
    REQUIRE(here[1]->name == "The Laughing Monk");
}

TEST_CASE("a row naming several maps belongs to none of them", "[buildings]") {
    // 20 of the shipped rows name more than one map, or something that is not
    // a map at all. Which one they belong to is not established, so returning
    // them for any single map would be a guess.
    const auto buildings = parsed();
    REQUIRE(buildings.entries()[2].map == "D2,C3,B1");
    REQUIRE(buildings.entries()[2].map_code().empty());
    REQUIRE(buildings.on_map("D2").empty());
    REQUIRE(buildings.on_map("B1").empty());
}

TEST_CASE("a map file name gives its code", "[buildings]") {
    REQUIRE(map_code_of("OutE3.Odm") == "E3");
    REQUIRE(map_code_of("outa1.odm") == "A1");
    // Indoor maps are never referenced by the table.
    REQUIRE(map_code_of("D01.blv").empty());
    REQUIRE(map_code_of("zddb02.blv").empty());
    REQUIRE(map_code_of("").empty());
}

TEST_CASE("an unknown map has no establishments", "[buildings]") {
    const auto buildings = parsed();
    REQUIRE(buildings.on_map("Z9").empty());
    REQUIRE(buildings.on_map("").empty());
}

TEST_CASE("a table without the expected header is refused", "[buildings]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("a\tb\tc\r\n1\t2\t3\r\n", table) == TextTableError::None);
    BuildingStatsTable out;
    REQUIRE(BuildingStatsTable::parse(table, out) == BuildingStatsError::NoHeader);
}
