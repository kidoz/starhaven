// Tests for reading a profession's benefit prose.
//
// Hermetic: each fixture is a phrasing the shipped `npcprof.txt` rows use.
#include <catch2/catch_test_macros.hpp>

#include "game/hire.hpp"

using starhaven::game::parse_benefit;

TEST_CASE("the teachers' experience bonuses parse from words and digits", "[hire]") {
    REQUIRE(parse_benefit("Unlimited item Identification and a 5 percent bonus on all "
                          "experience gained.")
                .experience_percent == 5);
    REQUIRE(parse_benefit("Ten percent bonus on all experience learned.").experience_percent ==
            10);
    REQUIRE(parse_benefit("Fifteen percent bonus on all experience learned.")
                .experience_percent == 15);
}

TEST_CASE("the bankers' and the pirate's gold bonuses parse", "[hire]") {
    REQUIRE(parse_benefit("Twenty percent bonus on all gold found.").gold_percent == 20);
    const auto pirate = parse_benefit(
        "All boat travel reduced by two days, gold is increased by ten percent when found, and "
        "Reputation is decreased by one full category.");
    REQUIRE(pirate.gold_percent == 10);
    REQUIRE(pirate.boat_days_faster == 2);
}

TEST_CASE("the guides shave their days from the right kind of travel", "[hire]") {
    REQUIRE(parse_benefit("All map crossings one day faster (minimum one day).")
                .coach_days_faster == 1);
    REQUIRE(parse_benefit("All map crossings three days faster (minimum one day).")
                .coach_days_faster == 3);
    REQUIRE(parse_benefit("All boat travel 2 days faster (minimum one day).").boat_days_faster ==
            2);
    REQUIRE(parse_benefit("Trips using the stables take two days fewer to complete (minimum "
                          "one day).")
                .coach_days_faster == 2);
    const auto explorer =
        parse_benefit("All travel times reduced by one day (minimum of one day).");
    REQUIRE(explorer.coach_days_faster == 1);
    REQUIRE(explorer.boat_days_faster == 1);
}

TEST_CASE("the healers' three rungs are told apart", "[hire]") {
    REQUIRE(parse_benefit("Cures all party hit points once a day.").heal_level == 1);
    REQUIRE(parse_benefit("Cures all party hit points and conditions (excepting dead, stoned, "
                          "or eradicated) once per day.")
                .heal_level == 2);
    REQUIRE(parse_benefit("Completely heals party of all hit points and conditions once per "
                          "day.")
                .heal_level == 3);
}

TEST_CASE("repairs, luck, protection, food and the dawn spells parse", "[hire]") {
    REQUIRE(parse_benefit("Unlimited weapon repair.").repairs_weapons);
    REQUIRE(parse_benefit("Unlimited armor repair.").repairs_armor);
    REQUIRE_FALSE(parse_benefit("Armor and weapon skills are increased by two points for each "
                                "character.")
                      .repairs_armor);
    REQUIRE(parse_benefit("Five point bonus to Luck statistic for all characters.").luck_bonus ==
            5);
    REQUIRE(parse_benefit("Perception skill is increased by five points and Luck statistics "
                          "are increased by ten points for each character.")
                .luck_bonus == 10);
    REQUIRE(parse_benefit("Increases protection from the four elements by 20 constantly.")
                .elemental_protection == 20);
    const auto cook = parse_benefit("Makes one day of food per day (maximum of 14 days).");
    REQUIRE(cook.food_per_day == 1);
    REQUIRE(cook.food_cap == 14);
    REQUIRE(parse_benefit("Casts the Bless spell (duration 2 hours) at master ranking once per "
                          "day.")
                .bless_hours == 2);
    REQUIRE(parse_benefit("Casts the Heroism spell (duration 2 hours) at master ranking once "
                          "per day.")
                .heroism_hours == 2);
}

TEST_CASE("a benefit this engine has no mechanic for parses to nothing", "[hire]") {
    REQUIRE_FALSE(parse_benefit("Grants a constant, single category bonus to your reputation.")
                      .any());
    REQUIRE_FALSE(parse_benefit("Keeps the Wizard Eye spell at expert ranking going at all "
                                "times.")
                      .any());
    REQUIRE_FALSE(parse_benefit("").any());
}

TEST_CASE("the porters' camping savings parse, gypsy phrasing included", "[hire]") {
    REQUIRE(parse_benefit("One less day of food use when camping, (minimum of one used).")
                .food_saved_camping == 1);
    REQUIRE(parse_benefit("Two less days of food use when camping, (minimum of one used).")
                .food_saved_camping == 2);
    REQUIRE(parse_benefit("Food use is reduced by one day's worth of food when resting "
                          "(minimum one day), Merchant skill is increased by three points for "
                          "each character, and Reputation is decreased by one full category.")
                .food_saved_camping == 1);
}

TEST_CASE("the masters' skill bonuses parse", "[hire]") {
    REQUIRE(parse_benefit("Two point bonus to all weapon skills for all characters.")
                .weapon_skill_bonus == 2);
    REQUIRE(parse_benefit("Armor and weapon skills are increased by two points for each "
                          "character.")
                .weapon_skill_bonus == 2);
    REQUIRE(parse_benefit("Four point bonus to all spell skills for all characters.")
                .spell_skill_bonus == 4);
    REQUIRE(parse_benefit("Four point bonus to Merchant skill for all characters.")
                .merchant_skill_bonus == 4);
    REQUIRE(parse_benefit("Merchant skill is increased by eight points for each character and "
                          "Reputation is decreased by one full category.")
                .merchant_skill_bonus == 8);
}

TEST_CASE("the scholar's unlimited eye parses", "[hire]") {
    REQUIRE(parse_benefit("Unlimited item Identification and a 5 percent bonus on all "
                          "experience gained.")
                .identifies);
    REQUIRE_FALSE(parse_benefit("Unlimited weapon repair.").identifies);
}

TEST_CASE("the trap trades' bonuses parse", "[hire]") {
    REQUIRE(parse_benefit("Four point bonus to Disarm Traps skill for all characters.")
                .disarm_bonus == 4);
    REQUIRE(parse_benefit("Disarm Traps skill is increased by eight points for each character "
                          "and Reputation is decreased by one full category.")
                .disarm_bonus == 8);
    REQUIRE(parse_benefit("Six point bonus to perception skill for all characters.")
                .perception_bonus == 6);
    REQUIRE(parse_benefit("Perception skill is increased by five points and Luck statistics "
                          "are increased by ten points for each character.")
                .perception_bonus == 5);
}
