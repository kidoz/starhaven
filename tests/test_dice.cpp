// Tests for the damage notation the design tables write.
//
// Hermetic: the strings are the two forms the shipped tables use.
#include <catch2/catch_test_macros.hpp>

#include "core/data/dice.hpp"

using namespace starhaven;
using namespace starhaven::data;

TEST_CASE("both spellings of a roll parse", "[dice]") {
    // The tables write "3d3" and "1D6+1"; all 212 monster attack damages and
    // all 78 weapons are one of the two forms.
    REQUIRE(parse_dice("3d3").count == 3);
    REQUIRE(parse_dice("3d3").sides == 3);
    REQUIRE(parse_dice("3d3").bonus == 0);
    REQUIRE(parse_dice("1D6+1").count == 1);
    REQUIRE(parse_dice("1D6+1").sides == 6);
    REQUIRE(parse_dice("1D6+1").bonus == 1);
    REQUIRE(parse_dice(" 2d6+2 ").sides == 6);
}

TEST_CASE("what is not a roll is not read as one", "[dice]") {
    // "0" is what the tables put where there is no attack, and it must not
    // come back as a roll of nothing that still deals the bonus.
    REQUIRE(parse_dice("0").empty());
    REQUIRE(parse_dice("").empty());
    REQUIRE(parse_dice("Phys").empty());
    REQUIRE(parse_dice("3d").empty());
    REQUIRE(parse_dice("d6").empty());
    REQUIRE(parse_dice("3d3x").empty());
    REQUIRE(parse_dice("3d3+").empty());
}

TEST_CASE("a roll stays inside its range", "[dice]") {
    const Dice dice = parse_dice("2d6+2");
    REQUIRE(dice.lowest() == 4);
    REQUIRE(dice.highest() == 14);

    Mm6Random random{12345};
    for (int i = 0; i < 500; ++i) {
        const int value = roll(dice, random);
        REQUIRE(value >= dice.lowest());
        REQUIRE(value <= dice.highest());
    }
}

TEST_CASE("dice are rolled one at a time", "[dice]") {
    // 2d6 is not 1d11+1: the middle of the range has to come up more often
    // than the ends, or every damage figure in the game is wrong.
    Mm6Random random{7};
    int middle = 0;
    int ends = 0;
    for (int i = 0; i < 2000; ++i) {
        const int value = roll(parse_dice("2d6"), random);
        if (value == 7) {
            ++middle;
        }
        if (value == 2 || value == 12) {
            ++ends;
        }
    }
    REQUIRE(middle > ends);
}

TEST_CASE("an empty roll is worth nothing", "[dice]") {
    Mm6Random random{1};
    REQUIRE(roll(Dice{}, random) == 0);
    REQUIRE(roll(parse_dice("0"), random) == 0);
}

#include "core/data/treasure.hpp"

TEST_CASE("a treasure code is a chance, a roll of gold and an item", "[dice]") {
    // "5%6D20+L2Bow": all 145 shipped codes parse into these parts.
    const Treasure full = parse_treasure("5%6D20+L2Bow");
    REQUIRE(full.chance == 5);
    REQUIRE(full.gold.count == 6);
    REQUIRE(full.gold.sides == 20);
    REQUIRE(full.item_level == 2);
    REQUIRE(full.item_kind == "Bow");

    // Gold alone, and with no chance prefix it always drops.
    const Treasure gold = parse_treasure("4D6");
    REQUIRE(gold.chance == 100);
    REQUIRE(gold.gold.count == 4);
    REQUIRE(gold.item_level == 0);

    // An item alone.
    const Treasure ring = parse_treasure("5%L2Ring");
    REQUIRE(ring.chance == 5);
    REQUIRE(ring.gold.empty());
    REQUIRE(ring.item_level == 2);
    REQUIRE(ring.item_kind == "Ring");

    // And an unnamed kind is any kind.
    REQUIRE(parse_treasure("400D10+L6").item_kind.empty());
    REQUIRE(parse_treasure("400D10+L6").item_level == 6);
}

TEST_CASE("a monster with nothing to leave leaves nothing", "[dice]") {
    REQUIRE(parse_treasure("0").empty());
    REQUIRE(parse_treasure("").empty());
    REQUIRE(parse_treasure("nonsense").empty());
}
