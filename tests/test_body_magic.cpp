// Tests for the Body school's numbers as read out of MM6.exe's spell switch.
//
// Hermetic: every figure here is a constant traced from a case body, and the
// assertions are the same ones SPELLS.TXT's prose makes where it makes any.
#include <catch2/catch_test_macros.hpp>

#include "game/body_magic.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("First Aid heals by rank alone", "[body]") {
    // "Cures 5 hit points" / "7" / "10" — the case takes no skill at all.
    REQUIRE(first_aid_heal(0) == 5);
    REQUIRE(first_aid_heal(1) == 7);
    REQUIRE(first_aid_heal(2) == 10);
    REQUIRE(first_aid_heal(0) == first_aid_heal(0));
    // Out-of-band ranks clamp rather than read past the array.
    REQUIRE(first_aid_heal(-1) == 5);
    REQUIRE(first_aid_heal(9) == 10);
}

TEST_CASE("the two skilled cures follow their own formulas", "[body]") {
    // "five plus 2 per point of skill", at every rank.
    REQUIRE(cure_wounds_heal(0) == 5);
    REQUIRE(cure_wounds_heal(10) == 25);
    // Power Cure is the same shape with ten in place of five.
    REQUIRE(power_cure_heal(0) == 10);
    REQUIRE(power_cure_heal(10) == 30);
    // And it is the only one of the three that reaches everyone.
    REQUIRE(heal_reaches_party(kSpellPowerCure));
    REQUIRE_FALSE(heal_reaches_party(kSpellCureWounds));
    REQUIRE_FALSE(heal_reaches_party(kSpellFirstAid));
}

TEST_CASE("the traced heal answers only for the three that have one", "[body]") {
    REQUIRE(traced_heal(kSpellFirstAid, 30, 2) == 10);
    REQUIRE(traced_heal(kSpellCureWounds, 7, 0) == 19);
    REQUIRE(traced_heal(kSpellPowerCure, 7, 0) == 24);
    // Harm and the cures carry no healing number of their own.
    REQUIRE(traced_heal(kSpellHarm, 7, 2) == 0);
    REQUIRE(traced_heal(kSpellCurePoison, 7, 2) == 0);
}

TEST_CASE("Speed and Power share one ladder", "[body]") {
    // "10 points plus 2 per point", "plus 3" at expert; master widens the
    // reach instead of raising the number again.
    REQUIRE(body_stat_bonus(5, 0) == 20);
    REQUIRE(body_stat_bonus(5, 1) == 25);
    REQUIRE(body_stat_bonus(5, 2) == 25);
    REQUIRE_FALSE(body_buff_hits_party(1));
    REQUIRE(body_buff_hits_party(2));
    // An hour a point, at every rank — a figure the table never states.
    REQUIRE(body_buff_minutes(3) == 180);
    REQUIRE(body_buff_minutes(0) == 60);
}

TEST_CASE("Protection from Poison scales one, two, three a point", "[body]") {
    REQUIRE(poison_shield(6, 0) == 6);
    REQUIRE(poison_shield(6, 1) == 12);
    REQUIRE(poison_shield(6, 2) == 18);
}

TEST_CASE("the cure window is three of each unit a point", "[body]") {
    // Three minutes a point at normal — the one figure the prose also gives.
    REQUIRE(cure_window_minutes(4, 0) == 12);
    // Three hours a point at expert, three days at master.
    REQUIRE(cure_window_minutes(4, 1) == 4 * 180);
    REQUIRE(cure_window_minutes(4, 2) == 4 * 4320);
    // No skill still buys one point's worth rather than nothing.
    REQUIRE(cure_window_minutes(0, 0) == 3);
    // The ladder climbs strictly.
    REQUIRE(cure_window_minutes(1, 0) < cure_window_minutes(1, 1));
    REQUIRE(cure_window_minutes(1, 1) < cure_window_minutes(1, 2));
}

TEST_CASE("the school's voices step by ten from seven thousand", "[body]") {
    REQUIRE(body_spell_sound(kSpellCureWeakness) == 7000);
    REQUIRE(body_spell_sound(kSpellFirstAid) == 7010);
    REQUIRE(body_spell_sound(kSpellCurePoison) == 7050);
    REQUIRE(body_spell_sound(kSpellCureDisease) == 7070);
    REQUIRE(body_spell_sound(kSpellPowerCure) == 7100);
}
