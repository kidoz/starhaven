// Tests for the damage a weapon's special enchantment adds, as read out of
// MM6.exe's post-hit walk.
//
// Hermetic: every band here is a constant traced from a case body, and the
// game's own special-bonus table carries none of them.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

#include "game/weapon_specials.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("the three cold specials climb in the table's order", "[specials]") {
    const auto* cold = special_rider(4);
    const auto* frost = special_rider(5);
    const auto* ice = special_rider(6);
    REQUIRE(cold != nullptr);
    REQUIRE(frost != nullptr);
    REQUIRE(ice != nullptr);
    REQUIRE(cold->low == 3);
    REQUIRE(cold->high == 4);
    REQUIRE(frost->low == 6);
    REQUIRE(frost->high == 8);
    REQUIRE(ice->low == 9);
    REQUIRE(ice->high == 12);
    // Each answers the cold column.
    REQUIRE(element_column(cold->element) == "Cold");
    REQUIRE(element_column(ice->element) == "Cold");
}

TEST_CASE("each element's three specials climb, and none overlap", "[specials]") {
    struct Ladder {
        int a;
        int b;
        int c;
        std::string_view column;
    };
    const std::array<Ladder, 4> ladders{{{4, 5, 6, "Cold"},
                                         {7, 8, 9, "Elec"},
                                         {10, 11, 12, "Fire"},
                                         {13, 14, 15, "Poison"}}};
    for (const auto& rung : ladders) {
        const auto* low = special_rider(rung.a);
        const auto* mid = special_rider(rung.b);
        const auto* high = special_rider(rung.c);
        REQUIRE(low != nullptr);
        REQUIRE(mid != nullptr);
        REQUIRE(high != nullptr);
        REQUIRE(low->low < mid->low);
        REQUIRE(mid->low < high->low);
        REQUIRE(element_column(low->element) == rung.column);
        REQUIRE(element_column(mid->element) == rung.column);
        REQUIRE(element_column(high->element) == rung.column);
    }
}

TEST_CASE("the poison three are flat, the rest roll a band", "[specials]") {
    for (const int id : {13, 14, 15}) {
        const auto* rider = special_rider(id);
        REQUIRE(rider != nullptr);
        REQUIRE(rider->low == rider->high);
    }
    REQUIRE(special_rider(13)->low == 5);
    REQUIRE(special_rider(14)->low == 8);
    REQUIRE(special_rider(15)->low == 12);
    // of Infernos is the narrowest band, of The Dragon the widest.
    REQUIRE(special_rider(12)->high - special_rider(12)->low == 2);
    REQUIRE(special_rider(46)->low == 10);
    REQUIRE(special_rider(46)->high == 20);
}

TEST_CASE("the specials that add nothing are not on the list", "[specials]") {
    // Vampiric and of Darkness drain instead, and share a case with Mordred.
    REQUIRE(special_rider(kVampiricSpecial) == nullptr);
    REQUIRE(special_rider(kDarknessSpecial) == nullptr);
    REQUIRE(special_drains(kVampiricSpecial, 0));
    REQUIRE(special_drains(kDarknessSpecial, 0));
    REQUIRE(special_drains(0, kMordred));
    REQUIRE_FALSE(special_drains(4, 0));
    // A fifth of the blow, floored.
    REQUIRE(vampiric_gain(50) == 10);
    REQUIRE(vampiric_gain(4) == 0);
    // And the untouched rows carry no rider at all.
    REQUIRE(special_rider(1) == nullptr);   // of Protection
    REQUIRE(special_rider(59) == nullptr);  // of Swiftness
    REQUIRE(special_rider(0) == nullptr);
}

TEST_CASE("two artifacts are named by id, not by enchantment", "[specials]") {
    REQUIRE(artifact_extra(kHades) == 20);
    REQUIRE(artifact_extra(kAres) == 30);
    REQUIRE(artifact_extra(kMordred) == 0);
    REQUIRE(artifact_extra(1) == 0);
}
