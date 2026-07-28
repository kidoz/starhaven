// Tests for reading the numbers out of a spell's prose: every phrasing the
// shipped table uses, and nothing invented for the ones that state none.
#include <catch2/catch_test_macros.hpp>

#include "core/data/spell_effects.hpp"

using namespace starhaven;
using namespace starhaven::data;

namespace {

SpellStatsEntry spell(const char* description, const char* normal = "",
                      const char* expert = "", const char* master = "") {
    SpellStatsEntry out;
    out.description = description;
    out.normal = normal;
    out.expert = expert;
    out.master = master;
    return out;
}

}  // namespace

TEST_CASE("flat damage reads from its own phrasing", "[spells]") {
    // Static Charge's wording.
    const auto effect = parse_spell_effect(
        spell("It only does 2-6 points of damage, but it always hits."), 0);
    REQUIRE(effect.damage.low == 2);
    REQUIRE(effect.damage.high == 6);
    REQUIRE(effect.damage_per_skill.empty());
    REQUIRE(effect.heal.empty());
}

TEST_CASE("scaling damage reads with and without a flat part", "[spells]") {
    // Fire Bolt: pure scaling.
    const auto bolt = parse_spell_effect(
        spell("Damage is 1-4 points of damage per point of skill in Fire Magic."), 0);
    REQUIRE(bolt.damage.empty());
    REQUIRE(bolt.damage_per_skill.low == 1);
    REQUIRE(bolt.damage_per_skill.high == 4);

    // Acid Burst: a base plus a roll per skill point.
    const auto acid = parse_spell_effect(
        spell("It always hits and does 9 points of damage plus 1-9 per point of skill."), 0);
    REQUIRE(acid.damage.low == 9);
    REQUIRE(acid.damage.high == 9);
    REQUIRE(acid.damage_per_skill.low == 1);
    REQUIRE(acid.damage_per_skill.high == 9);
}

TEST_CASE("healing reads from the rank cell first, then the description", "[spells]") {
    // First Aid: the rank cells differ by mastery.
    const auto aid = spell("Cures 5 hit points on a single target when cast.", "Cures 5 hit points",
                           "Cures 7 hit points", "Cures 10 hit points");
    REQUIRE(parse_spell_effect(aid, 0).heal.low == 5);
    REQUIRE(parse_spell_effect(aid, 1).heal.low == 7);
    REQUIRE(parse_spell_effect(aid, 2).heal.low == 10);

    // Healing Touch: the amount only in the description.
    const auto touch = parse_spell_effect(
        spell("Cheaply heals a single character of 3-7 hit points."), 0);
    REQUIRE(touch.heal.low == 3);
    REQUIRE(touch.heal.high == 7);
}

TEST_CASE("prose without numbers casts nothing", "[spells]") {
    REQUIRE(parse_spell_effect(spell("Halves damage from incoming ranged attacks."), 0).empty());
    REQUIRE(parse_spell_effect(
                spell("Duration 1 hour per point of skill.", "Moderate recovery rate"), 0)
                .empty());
}
