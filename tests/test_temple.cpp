// Tests for the temples' own terms: the Val price and the service ceiling
// the stock cell writes.
#include <catch2/catch_test_macros.hpp>

#include "game/temple.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::BuildingStatsEntry temple(const char* services, float value) {
    data::BuildingStatsEntry shop;
    shop.type = "Temple";
    shop.stock_a = services;
    shop.price_factor = value;
    return shop;
}

}  // namespace

TEST_CASE("a temple's ceiling is its own cell", "[temple]") {
    REQUIRE(is_temple(temple("No Errad", 10)));

    const TempleService common = temple_service(temple("No Errad", 10));
    REQUIRE(common.heals_dead);
    REQUIRE(common.heals_stone);
    REQUIRE_FALSE(common.heals_eradicated);

    // Temple Stone mends everything; Temple Baa very little.
    const TempleService stone = temple_service(temple("All OK", 40));
    REQUIRE(stone.heals_eradicated);
    const TempleService baa = temple_service(temple("No Dead,Stone,Errad", 2));
    REQUIRE_FALSE(baa.heals_dead);
    REQUIRE_FALSE(baa.heals_stone);
    REQUIRE_FALSE(baa.heals_eradicated);
}

TEST_CASE("the price is the row's own Val", "[temple]") {
    REQUIRE(heal_price(temple("No Errad", 10)) == 10);
    REQUIRE(heal_price(temple("No Dead,Stone,Errad", 2)) == 2);
    REQUIRE(heal_price(temple("All OK", 0)) == 1);
}
