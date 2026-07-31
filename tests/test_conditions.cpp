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
