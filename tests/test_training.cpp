// Tests for training halls: the ceilings the sheet writes, the curve and
// the fee this engine supplies.
#include <catch2/catch_test_macros.hpp>

#include "game/training.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::BuildingStatsEntry hall(const char* ceiling, float value) {
    data::BuildingStatsEntry shop;
    shop.type = "Training";
    shop.stock_a = ceiling;
    shop.price_factor = value;
    return shop;
}

}  // namespace

TEST_CASE("the ceiling is the sheet's own words", "[training]") {
    REQUIRE(max_level_of(hall("Max level = 15", 10)) == 15);
    REQUIRE(max_level_of(hall("Max Level = 200", 30)) == 200);
    // "No Max" writes no digits: no ceiling.
    REQUIRE(max_level_of(hall("No Max", 50)) == 0);
    REQUIRE(is_training(hall("Max level = 15", 10)));
}

TEST_CASE("the curve and the fee are the engine's, and say so", "[training]") {
    REQUIRE(experience_for_level(1) == 0);
    REQUIRE(experience_for_level(2) == 1000);
    REQUIRE(experience_for_level(3) == 3000);
    REQUIRE(experience_for_level(10) == 45000);
    REQUIRE(training_cost(hall("No Max", 10), 2) == 20);
    REQUIRE(training_cost(hall("No Max", 50), 4) == 200);
}

TEST_CASE("an offer knows ready, short, and beyond", "[training]") {
    Character who;
    who.level = 1;
    who.experience = 1500;
    who.max_hit_points = 30;
    who.hit_points = 12;

    const auto ready = training_offer(hall("Max level = 15", 10), who);
    REQUIRE(ready.to_level == 2);
    REQUIRE(ready.cost == 20);
    REQUIRE(ready.experience_needed == 0);

    who.experience = 400;
    REQUIRE(training_offer(hall("Max level = 15", 10), who).experience_needed == 600);

    who.level = 15;
    REQUIRE(training_offer(hall("Max level = 15", 10), who).to_level == 0);
    REQUIRE(training_offer(hall("No Max", 10), who).to_level == 16);

    // Training grants the level and this engine's own gains.
    who.level = 1;
    who.experience = 1500;
    train(who);
    REQUIRE(who.level == 2);
    REQUIRE(who.max_hit_points == 35);
    REQUIRE(who.hit_points == 35);
}
