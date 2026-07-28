// Tests for riding the coach and sailing the boat: the route cells the
// stables and docks carry, read the way the sheet's own margin notes say.
//
// Hermetic: the map table is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/travel.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::MapStatsTable maps() {
    std::string s;
    s += "\tMap Stats\t\t\r\n";
    s += "\t\t\tReset\r\n";
    s += "#\tName\tFile name\t#\tDay\tDays\t0-10\t0-10\t0-6\t%\t%\t%\t%\tMon1 Pic\tMon 1\t 1-5\t#"
         "\tMon2 Pic\tMon 2\t 1-5\t#\tMon3 Pic\tMon 3\t 1-5\t#\tTrack\tMap Designer\r\n";
    s += "1\tBootleg Bay East\tOutB3.Odm\t0\t0\t224\t8\t9\t6\t40\t50\t50\t0\t0\t0\t1\t 1-4\t0\t0"
         "\t1\t 1-4\t0\t0\t1\t 1-4\t5\tPeter\r\n";
    s += "2\tCastle Ironfist\tOutD3.Odm\t0\t0\t168\t7\t8\t6\t20\t40\t30\t30\t0\t0\t1\t 1-4\t0\t0"
         "\t1\t 1-4\t0\t0\t1\t 1-4\t7\t\r\n";
    s += "\t\t\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(s, table) == data::TextTableError::None);
    data::MapStatsTable out;
    REQUIRE(data::MapStatsTable::parse(table, out) == data::MapStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("a route cell names a destination, its days, and its length", "[travel]") {
    const auto table = maps();
    const TravelRoute route = parse_route("Castle Ironfist D3,M,W,F,2", table);
    REQUIRE_FALSE(route.empty());
    REQUIRE(route.destination == "Castle Ironfist");
    // The area code is the join: D3 is OutD3.Odm.
    REQUIRE(route.map_file == "OutD3.Odm");
    REQUIRE(route.days == 2);
    REQUIRE(route.leaves[1]);  // Monday
    REQUIRE(route.leaves[3]);  // Wednesday
    REQUIRE(route.leaves[5]);  // Friday
    REQUIRE_FALSE(route.leaves[0]);
    // Day one is a Sunday, so day two is a Monday and the coach leaves.
    REQUIRE_FALSE(route.leaves_on(0));
    REQUIRE(route.leaves_on(1));
}

TEST_CASE("the sheet writes Tuesday two ways and Sunday two ways", "[travel]") {
    const auto table = maps();
    REQUIRE(parse_route("Castle Ironfist D3,T,F,4", table).leaves[2]);
    REQUIRE(parse_route("Castle Ironfist D3,Tu,F,4", table).leaves[2]);
    REQUIRE(parse_route("Castle Ironfist D3,Sun,1", table).leaves[0]);
    REQUIRE(parse_route("Castle Ironfist D3,Su,1", table).leaves[0]);
}

TEST_CASE("a destination without an area code resolves by its name", "[travel]") {
    // "Bootleg Bay East,Tu,F,3" writes no code; the display name is the join.
    const auto table = maps();
    const TravelRoute route = parse_route("Bootleg Bay East,Tu,F,3", table);
    REQUIRE(route.map_file == "OutB3.Odm");
    REQUIRE(route.days == 3);
}

TEST_CASE("what cannot resolve stays off the timetable", "[travel]") {
    const auto table = maps();
    REQUIRE(parse_route("-", table).empty());
    REQUIRE(parse_route("", table).empty());
    // The Enterprise's cell is a designer note, not a route.
    REQUIRE(parse_route("Any outdoor area,\"Where to?\", set to start location,1", table).empty());
}

TEST_CASE("a travel row's three cells become its timetable", "[travel]") {
    const auto table = maps();
    data::BuildingStatsEntry shop;
    shop.type = "Stables";
    shop.stock_a = "Castle Ironfist D3,M,W,F,2";
    shop.stock_b = "Bootleg Bay East,Tu,4";
    shop.stock_c = "-";
    shop.price_factor = 2.0f;
    REQUIRE(is_travel(shop));
    const auto routes = routes_of(shop, table);
    REQUIRE(routes.size() == 2);
    REQUIRE(routes[0].map_file == "OutD3.Odm");
    REQUIRE(routes[1].map_file == "OutB3.Odm");
    // The fare is the row's Val, scaled by this engine's own constant.
    REQUIRE(fare_of(shop) == 2 * kFarePerVal);

    shop.type = "Weapon Shop";
    REQUIRE_FALSE(is_travel(shop));
}
