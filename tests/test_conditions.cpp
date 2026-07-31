// Tests for the condition numbering read out of MM6.exe's own run.
#include <catch2/catch_test_macros.hpp>

#include "game/conditions.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("the three cure spells push the ids they lift", "[conditions]") {
    // Cure Weakness 1, Cure Poison 6, Cure Disease 7 — read from the cases.
    REQUIRE(kConditionWeakId == 1);
    REQUIRE(kConditionPoisoned == 6);
    REQUIRE(kConditionDiseased == 7);
    // Poison and disease are adjacent because MM6 carries one level of each.
    REQUIRE(kConditionDiseased == kConditionPoisoned + 1);
}

TEST_CASE("the run's offsets are eight bytes apart from 0x1380", "[conditions]") {
    REQUIRE(condition_offset(0) == 0x1380);
    REQUIRE(condition_offset(2) == 0x1390);
    REQUIRE(condition_offset(12) == 0x13e0);
    REQUIRE(condition_offset(13) == 0x13e8);
    REQUIRE(condition_offset(14) == 0x13f0);
    REQUIRE(condition_offset(15) == 0x13f8);
    REQUIRE(condition_offset(16) == 0x1400);
}

TEST_CASE("the incapacitating six and the unhealable two", "[conditions]") {
    // The AI's own set, at 0x404cdf.
    REQUIRE(kIncapacitating.size() == 6);
    for (const int id : {2, 12, 13, 14, 15, 16}) {
        REQUIRE(incapacitates(id));
    }
    REQUIRE_FALSE(incapacitates(kConditionPoisoned));
    REQUIRE_FALSE(incapacitates(kConditionAfraid));
    // The heal's own two, at 0x47fb60.
    REQUIRE(beyond_healing(kConditionDead));
    REQUIRE(beyond_healing(kConditionEradicated));
    REQUIRE_FALSE(beyond_healing(kConditionUnconscious));
    // Everything beyond healing also takes a character out of the fight.
    for (const int id : kBeyondHealing) {
        REQUIRE(incapacitates(id));
    }
}

TEST_CASE("every named condition says its name", "[conditions]") {
    REQUIRE(condition_name(kConditionPoisoned) == "Poisoned");
    REQUIRE(condition_name(kConditionEradicated) == "Eradicated");
    REQUIRE(condition_name(kConditionZombie) == "Zombie");
    // The four the ordering does not reach stay nameless rather than guessed.
    REQUIRE(condition_name(8).empty());
    REQUIRE(condition_name(11).empty());
}

TEST_CASE("a condition scales the character's numbers", "[conditions]") {
    // The priority order read as condition ids is worst first.
    REQUIRE(kConditionPriority.size() == 14);
    REQUIRE(kConditionPriority.front() == kConditionEradicated);
    REQUIRE(kConditionPriority[2] == kConditionDead);
    REQUIRE(kConditionPriority.back() == kConditionDrunk);
    // And the per-condition multiplier is what being ill costs.
    REQUIRE(condition_percent(kConditionPoisoned) == 75);
    REQUIRE(condition_percent(kConditionDiseased) == 60);
    REQUIRE(condition_percent(kConditionDrunk) == 10);
    REQUIRE(condition_percent(kConditionAfraid) == 50);
    // The unafflicted and the unlisted pay nothing.
    REQUIRE(condition_percent(kConditionCursed) == 100);
    REQUIRE(condition_percent(-1) == 100);
    REQUIRE(condition_percent(99) == 100);
    // The walk takes the worst that is set, in the table's own order.
    const auto held = [](int id) { return id == kConditionPoisoned || id == kConditionDead; };
    REQUIRE(worst_condition(held) == kConditionDead);
    const auto only_poison = [](int id) { return id == kConditionPoisoned; };
    REQUIRE(worst_condition(only_poison) == kConditionPoisoned);
    const auto none = [](int) { return false; };
    REQUIRE(worst_condition(none) == -1);
    REQUIRE(condition_percent(worst_condition(none)) == 100);
}
