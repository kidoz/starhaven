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

TEST_CASE("a spell's reach is read from its own prose", "[spells]") {
    // The designers' own words: a single target unless the description
    // says the blast catches others or names everything in sight.
    REQUIRE(parse_spell_effect(spell("Does 5 points of damage."), 0).reach ==
            SpellReach::Single);
    REQUIRE(parse_spell_effect(
                spell("targets a single monster, but explodes to hurt anyone else caught in "
                      "the blast."),
                0)
                .reach == SpellReach::Blast);
    REQUIRE(parse_spell_effect(spell("Summons flaming rocks from the sky in a large radius "
                                     "surrounding your chosen target."),
                               0)
                .reach == SpellReach::Blast);
    REQUIRE(parse_spell_effect(
                spell("Inflicts 25 points of damage plus 1 per point of skill on all "
                      "creatures in sight."),
                0)
                .reach == SpellReach::Sight);
}

TEST_CASE("prose without numbers casts nothing", "[spells]") {
    REQUIRE(parse_spell_effect(spell("Halves damage from incoming ranged attacks."), 0).empty());
    REQUIRE(parse_spell_effect(
                spell("Duration 1 hour per point of skill.", "Moderate recovery rate"), 0)
                .empty());
}

TEST_CASE("durations read hours and per-skill minutes apart", "[spells]") {
    // The four buff spells all write it this way.
    const auto bless = parse_spell_duration(
        spell("", "Duration 1 hour + 5 minutes per point of skill"), 0);
    REQUIRE(bless.base_minutes == 60);
    REQUIRE(bless.per_skill_minutes == 5);
    REQUIRE(bless.minutes(4) == 80);

    const auto haste = parse_spell_duration(
        spell("", "Duration 1 hour + 1 minute per skill point"), 0);
    REQUIRE(haste.base_minutes == 60);
    REQUIRE(haste.per_skill_minutes == 1);

    // Minutes alone, scaling alone.
    const auto scaling = parse_spell_duration(
        spell("", "Duration 5 minutes per point of skill"), 0);
    REQUIRE(scaling.base_minutes == 0);
    REQUIRE(scaling.per_skill_minutes == 5);

    REQUIRE(parse_spell_duration(spell("", "Moderate recovery rate"), 0).empty());
}

TEST_CASE("a monster's spell cell parses whole, typos included", "[spells]") {
    const MonsterSpell cast = parse_monster_spell("Fireball,N,5");
    REQUIRE(cast.name == "Fireball");
    REQUIRE(cast.mastery == 0);
    REQUIRE(cast.skill == 5);
    REQUIRE(parse_monster_spell("Lightning Bolt,M,12").mastery == 2);
    REQUIRE(parse_monster_spell("0").empty());

    SpellStatsTable spells;
    // find_spell_tolerant absorbs the sheet's own two misspellings.
    // (Built through the table parser elsewhere; here the empty table just
    // answers nothing.)
    REQUIRE(find_spell_tolerant(spells, "Psychic Shockt") == nullptr);
}

TEST_CASE("a duration written only in the description is still read", "[spells]") {
    starhaven::data::SpellStatsEntry spell;
    spell.description =
        "All creatures in the caster's sight fear the caster and flee.  The duration of Mass "
        "Fear is 3 minutes per point of skill in Mind Magic.";
    spell.normal = "Moderate recovery rate";
    const auto duration = starhaven::data::parse_spell_duration(spell, 0);
    REQUIRE(duration.per_skill_minutes == 3);
    REQUIRE(duration.base_minutes == 0);
    REQUIRE(duration.minutes(5) == 15);

    // A description with numbers but no scaling phrase is not a clock.
    starhaven::data::SpellStatsEntry other;
    other.description = "Does 25 points of damage to the 4 nearest monsters.";
    other.normal = "";
    REQUIRE(starhaven::data::parse_spell_duration(other, 0).empty());
}

TEST_CASE("the cures' first sentences name their conditions", "[spells]") {
    starhaven::data::SpellStatsEntry spell;
    spell.description = "Cures poison in a character if you cast this spell in time.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).poison);

    spell.description = "Removes the afraid condition from a character if you cast this spell.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).affliction == "Affraid");

    spell.description =
        "Automatically awakens all of your characters from a normal sleep and will awaken them "
        "from a magical sleep.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).affliction == "Asleep");

    spell.description = "Cures paralysis if you cast this spell in time.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).affliction == "Paralyze");

    spell.description = "Removes the cursed condition from a character.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).affliction == "Curse");

    spell.description = "Does 2-6 points of damage.";
    REQUIRE(starhaven::data::parse_spell_cure(spell).empty());
}

TEST_CASE("a duration written as 'lasts' is read like one", "[spells]") {
    starhaven::data::SpellStatsEntry spell;
    spell.description = "The spell lasts 1 hour per point of skill in Air Magic.";
    spell.normal = "Only shows terrain and monsters";
    const auto duration = starhaven::data::parse_spell_duration(spell, 0);
    REQUIRE(duration.per_skill_minutes == 60);
}

TEST_CASE("days and weeks read as their minutes", "[spells]") {
    const auto days = starhaven::data::parse_duration_text("decays in 1 day per point of skill");
    REQUIRE(days.per_skill_minutes == 60 * 24);
    const auto weeks =
        starhaven::data::parse_duration_text("decays in 1 week per point of skill");
    REQUIRE(weeks.per_skill_minutes == 60 * 24 * 7);
}
