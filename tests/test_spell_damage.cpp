// Tests for the damage every thrown spell rolls, as read out of MM6.exe's
// own damage routine.
//
// Hermetic: every figure is a constant traced from a case body, and
// SPELLS.TXT carries none of them.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "game/spell_damage.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {
// A source that always rolls the top of a die, then one that always rolls
// the bottom, so a band can be checked exactly.
struct Highest {
    // One below the least common multiple of 1..25, so `x % sides` is
    // `sides - 1` for every die the table uses.
    std::uint64_t operator()() { return 26771144400ULL - 1ULL; }
};
struct Lowest {
    std::uint64_t operator()() { return 0ULL; }
};
}  // namespace

TEST_CASE("thirty-four spells have a case and the rest have none", "[spelldmg]") {
    REQUIRE(kSpellDamage.size() == 34);
    // The damage spells, by school.
    REQUIRE(spell_rolls_damage(2));   // Flame Arrow
    REQUIRE(spell_rolls_damage(18));  // Lightning Bolt
    REQUIRE(spell_rolls_damage(70));  // Harm
    REQUIRE(spell_rolls_damage(97));  // Dragon Breath
    REQUIRE(spell_rolls_damage(98));  // Armageddon shares the last case
    REQUIRE(spell_rolls_damage(99));  // and so does Dark Containment
    // The rest roll nothing at all: the protections, the cures, the buffs.
    REQUIRE_FALSE(spell_rolls_damage(1));   // Torch Light
    REQUIRE_FALSE(spell_rolls_damage(3));   // Protection from Fire
    REQUIRE_FALSE(spell_rolls_damage(68));  // First Aid
    REQUIRE_FALSE(spell_rolls_damage(73));  // Speed
    REQUIRE_FALSE(spell_rolls_damage(0));
    REQUIRE_FALSE(spell_rolls_damage(200));
}

TEST_CASE("the unscaled spells match the bands their rows print", "[spelldmg]") {
    // Every one of these is stated in words by SPELLS.TXT, which is what
    // caught three of them being one point high the first time round.
    struct Band {
        int spell;
        int low;
        int high;
    };
    const std::array<Band, 5> bands{{
        {2, 1, 8},    // Flame Arrow  "1-8 points"
        {13, 2, 6},   // Static Charge "2-6 points"
        {24, 2, 6},   // Cold Beam    "2-6 points"
        {35, 3, 8},   // Magic Arrow  "3-8 points"
        {45, 1, 6},   // Spirit Arrow "1-6 points"
    }};
    for (const auto& band : bands) {
        REQUIRE(roll_spell_damage(band.spell, 0, Lowest{}) == band.low);
        REQUIRE(roll_spell_damage(band.spell, 30, Lowest{}) == band.low);
        REQUIRE(roll_spell_damage(band.spell, 0, Highest{}) == band.high);
        REQUIRE(roll_spell_damage(band.spell, 30, Highest{}) == band.high);
    }
}

TEST_CASE("the skill-scaled spells roll one die a point", "[spelldmg]") {
    // Fire Bolt is skill d4 and nothing besides.
    REQUIRE(roll_spell_damage(4, 0, Highest{}) == 0);
    REQUIRE(roll_spell_damage(4, 1, Highest{}) == 4);
    REQUIRE(roll_spell_damage(4, 5, Highest{}) == 20);
    REQUIRE(roll_spell_damage(4, 5, Lowest{}) == 5);
    // Incinerate is skill d15 + 15, so the flat part stands even at no skill.
    REQUIRE(roll_spell_damage(11, 0, Highest{}) == 15);
    REQUIRE(roll_spell_damage(11, 2, Highest{}) == 45);
    // Dragon Breath rolls the biggest die in the game.
    REQUIRE(roll_spell_damage(97, 1, Highest{}) == 25);
}

TEST_CASE("the diceless spells are skill plus a constant", "[spelldmg]") {
    // Ring of Fire, Meteor Shower and Inferno climb by six, eight and twelve.
    REQUIRE(roll_spell_damage(7, 10, Highest{}) == 16);
    REQUIRE(roll_spell_damage(9, 10, Highest{}) == 18);
    REQUIRE(roll_spell_damage(10, 10, Highest{}) == 22);
    // The rolls do not vary, because nothing is rolled.
    REQUIRE(roll_spell_damage(7, 10, Lowest{}) == roll_spell_damage(7, 10, Highest{}));
    // Armageddon and Dark Containment share the largest constant.
    REQUIRE(roll_spell_damage(98, 10, Lowest{}) == 60);
    REQUIRE(roll_spell_damage(99, 10, Lowest{}) == 60);
}

TEST_CASE("the ranges say the same thing the dice do", "[spelldmg]") {
    data::SpellRange flat;
    data::SpellRange scaled;
    // skill d4: nothing flat, one to four a point.
    REQUIRE(traced_damage_ranges(4, 7, flat, scaled));
    REQUIRE(flat.low == 0);
    REQUIRE(scaled.low == 1);
    REQUIRE(scaled.high == 4);
    // 1d8: a flat one to eight, nothing per point.
    REQUIRE(traced_damage_ranges(2, 7, flat, scaled));
    REQUIRE(flat.low == 1);
    REQUIRE(flat.high == 8);
    REQUIRE(scaled.empty());
    // Magic Arrow's band is three to eight, not four to nine.
    REQUIRE(traced_damage_ranges(35, 7, flat, scaled));
    REQUIRE(flat.low == 3);
    REQUIRE(flat.high == 8);
    // skill + 6: a fixed flat, nothing rolled.
    REQUIRE(traced_damage_ranges(7, 7, flat, scaled));
    REQUIRE(flat.low == 13);
    REQUIRE(flat.high == 13);
    REQUIRE(scaled.empty());
    // And a spell with no case leaves the prose to answer.
    REQUIRE_FALSE(traced_damage_ranges(68, 7, flat, scaled));
}

TEST_CASE("the school ladders climb", "[spelldmg]") {
    // Fire: Flame Arrow, Fire Bolt, Fireball, Incinerate.
    REQUIRE(roll_spell_damage(4, 10, Highest{}) < roll_spell_damage(6, 10, Highest{}));
    REQUIRE(roll_spell_damage(6, 10, Highest{}) < roll_spell_damage(11, 10, Highest{}));
    // Air: Sparks, Lightning Bolt, Implosion.
    REQUIRE(roll_spell_damage(15, 10, Highest{}) < roll_spell_damage(18, 10, Highest{}));
    REQUIRE(roll_spell_damage(18, 10, Highest{}) < roll_spell_damage(20, 10, Highest{}));
    // Dark's own: Toxic Cloud under Dragon Breath at a decent skill.
    REQUIRE(roll_spell_damage(90, 20, Highest{}) < roll_spell_damage(97, 20, Highest{}));
}
