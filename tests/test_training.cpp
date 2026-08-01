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

    // Training grants the level, and what the level is worth now comes from
    // the class tables rather than a flat number. A Knight with Endurance
    // at the pivot gets its four.
    who.level = 1;
    who.experience = 1500;
    who.class_name = "Knight";
    who.attributes[static_cast<std::size_t>(Attribute::Endurance)] = 13;
    who.max_hit_points = class_hit_points("Knight", 1, 0);
    who.hit_points = who.max_hit_points;
    const int before = who.max_hit_points;
    train(who);
    REQUIRE(who.level == 2);
    REQUIRE(who.max_hit_points - before == kClassHitPointsPerLevel[class_id("Knight")]);
    REQUIRE(who.hit_points == who.max_hit_points);
    // And the wounds a character carries are kept across a level.
    who.hit_points -= 7;
    const int wounded = who.max_hit_points - who.hit_points;
    train(who);
    REQUIRE(who.max_hit_points - who.hit_points == wounded);
}

TEST_CASE("a guild teaches what the class may hold", "[training]") {
    game::Character who;
    who.class_name = "Cleric";
    who.skills[std::string(game::kSkillNames[game::skill_id("Mace")])] = 3;
    int gold = 100;
    // A cleric may learn Chain; a knight's magic is refused whatever it pays.
    REQUIRE(game::learn_skill(who, game::skill_id("Chain"), 40, gold) ==
            game::TeachRefusal::None);
    REQUIRE(gold == 60);
    REQUIRE(game::skill_points(who.skills["Chain"]) == 1);
    REQUIRE(game::skill_rank(who.skills["Chain"]) == 0);
    // Twice is refused, and the purse is untouched.
    REQUIRE(game::learn_skill(who, game::skill_id("Chain"), 40, gold) ==
            game::TeachRefusal::AlreadyHolds);
    REQUIRE(gold == 60);
    // Plate is zero in the cleric's row: never, at any price.
    REQUIRE(game::learn_skill(who, game::skill_id("Plate"), 0, gold) ==
            game::TeachRefusal::ClassMayNot);
    // And a purse too thin buys nothing.
    REQUIRE(game::learn_skill(who, game::skill_id("Staff"), 999, gold) ==
            game::TeachRefusal::TooPoor);
    REQUIRE_FALSE(who.skills.contains("Staff"));
}

TEST_CASE("a rung costs the teacher's own price", "[training]") {
    game::Character who;
    who.class_name = "Cleric";
    who.skills["Mace"] = 3;
    int gold = 2000;
    // Master cannot be bought over novice's head.
    REQUIRE(game::buy_rank(who, game::skill_id("Mace"), 2, gold) ==
            game::TeachRefusal::NotNextRung);
    REQUIRE(game::buy_rank(who, game::skill_id("Mace"), 1, gold) == game::TeachRefusal::None);
    REQUIRE(gold == 0);
    REQUIRE(game::skill_rank(who.skills["Mace"]) == 1);
    REQUIRE(game::skill_points(who.skills["Mace"]) == 3);  // the points do not move
    // Master is five thousand, and an empty purse buys nothing.
    REQUIRE(game::buy_rank(who, game::skill_id("Mace"), 2, gold) == game::TeachRefusal::TooPoor);
    gold = 5000;
    REQUIRE(game::buy_rank(who, game::skill_id("Mace"), 2, gold) == game::TeachRefusal::None);
    REQUIRE(gold == 0);
    REQUIRE(game::skill_rank(who.skills["Mace"]) == 2);
    // A skill not held cannot be ranked.
    gold = 9999;
    REQUIRE(game::buy_rank(who, game::skill_id("Chain"), 1, gold) == game::TeachRefusal::NotHeld);
}

TEST_CASE("what is left to learn is what the class may hold", "[training]") {
    game::Character knight;
    knight.class_name = "Knight";
    const auto left = game::teachable_skills(knight);
    // A knight with nothing yet may be taught everything its row allows and
    // nothing it forbids.
    REQUIRE(!left.empty());
    for (const int slot : left) {
        REQUIRE(game::class_may_learn(game::class_id("Knight"), slot));
    }
    for (const int school : {game::skill_id("Fire"), game::skill_id("Dark"),
                             game::skill_id("Meditation"), game::skill_id("Thievery")}) {
        REQUIRE(std::find(left.begin(), left.end(), school) == left.end());
    }
    // What it already holds drops off the list.
    knight.skills["Sword"] = 1;
    const auto after = game::teachable_skills(knight);
    REQUIRE(std::find(after.begin(), after.end(), game::skill_id("Sword")) == after.end());
    REQUIRE(after.size() + 1 == left.size());
}
