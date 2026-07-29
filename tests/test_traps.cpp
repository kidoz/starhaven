// Tests for the chests' traps: the map's difficulty, the skills' say.
#include <catch2/catch_test_macros.hpp>

#include "game/traps.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("a map with no trap difficulty traps nothing", "[traps]") {
    for (int chest = 0; chest < 20; ++chest) {
        REQUIRE_FALSE(chest_trapped(0, chest));
    }
}

TEST_CASE("the difficulty is the chance in ten, steady per chest", "[traps]") {
    // At ten, every chest; and a chest answers the same way twice.
    for (int chest = 0; chest < 20; ++chest) {
        REQUIRE(chest_trapped(10, chest));
        REQUIRE(chest_trapped(3, chest) == chest_trapped(3, chest));
    }
    // At three in ten, some are and some are not.
    int trapped = 0;
    for (int chest = 0; chest < 20; ++chest) {
        trapped += chest_trapped(3, chest) ? 1 : 0;
    }
    REQUIRE(trapped > 0);
    REQUIRE(trapped < 20);
}

TEST_CASE("disarm reaches the map's number or does not", "[traps]") {
    REQUIRE(disarmed(4, 4));
    REQUIRE(disarmed(4, 9));
    REQUIRE_FALSE(disarmed(4, 3));
}

TEST_CASE("the blast rolls the difficulty's dice and spares the perceptive", "[traps]") {
    std::array<Character, 4> party;
    for (auto& who : party) {
        who.hit_points = 30;
        who.max_hit_points = 30;
    }
    party[3].hit_points = 0;  // the fallen are past hurting

    Mm6Random random{11};
    const TrapBlast blast = spring_trap(5, party, 0, random);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(blast.damage[i] >= 5);
        REQUIRE(blast.damage[i] <= 30);
    }
    REQUIRE(blast.damage[3] == 0);

    // Perception at twenty points avoids everything: the cap is 95, but
    // 100 points would be needed only past it.
    Mm6Random lucky{11};
    const TrapBlast avoided = spring_trap(5, party, 100, lucky);
    int total = 0;
    for (const int d : avoided.damage) {
        total += d;
    }
    // 95 percent avoidance: a few thousand rolls would still slip one
    // through, but four rolls at seed 11 do not.
    REQUIRE(total == 0);
}

TEST_CASE("locks roll their own dice, apart from the traps", "[traps]") {
    for (int chest = 0; chest < 20; ++chest) {
        REQUIRE_FALSE(chest_locked(0, chest));
        REQUIRE(chest_locked(10, chest));
    }
    // The two rolls disagree somewhere: a chest can be one and not the other.
    bool differs = false;
    for (int chest = 0; chest < 20 && !differs; ++chest) {
        differs = chest_locked(5, chest) != chest_trapped(5, chest);
    }
    REQUIRE(differs);
}
