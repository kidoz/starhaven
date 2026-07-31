// Tests for the party: portrait naming, attribute bonuses, and what a
// starting party is made of.
//
// Hermetic: the name table is built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/party.hpp"
#include "game/rest.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::NameTable names() {
    std::string body = "Male\tFemale\r\n";
    body += "Aaron\tAlice\r\nAbe\tAllison\r\nAbel\tAmanda\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::NameTable out;
    REQUIRE(data::NameTable::parse(table, out) == data::NameTableError::None);
    return out;
}

}  // namespace

TEST_CASE("a face and a frame name an archive entry", "[party]") {
    // The twelve families are MaleA..MaleH then GirlA..GirlD, each with 53
    // frames numbered from one.
    REQUIRE(portrait_entry(0) == "MaleA01");
    REQUIRE(portrait_entry(7) == "MaleH01");
    REQUIRE(portrait_entry(8) == "GirlA01");
    REQUIRE(portrait_entry(11, 53) == "GirlD53");
    REQUIRE(portrait_entry(3, 9) == "MaleD09");
}

TEST_CASE("a face or frame outside the art names nothing", "[party]") {
    REQUIRE(portrait_entry(-1).empty());
    REQUIRE(portrait_entry(kFaceCount).empty());
    REQUIRE(portrait_entry(0, 0).empty());
    REQUIRE(portrait_entry(0, kPortraitFrameCount + 1).empty());
}

TEST_CASE("the last four faces are the female ones", "[party]") {
    REQUIRE_FALSE(face_is_female(0));
    REQUIRE_FALSE(face_is_female(kMaleFaceCount - 1));
    REQUIRE(face_is_female(kMaleFaceCount));
    REQUIRE(face_is_female(kFaceCount - 1));
}

TEST_CASE("the attribute bonus follows the executable's own ladder", "[party]") {
    REQUIRE(attribute_bonus(3) < attribute_bonus(7));
    REQUIRE(attribute_bonus(7) < attribute_bonus(12));
    REQUIRE(attribute_bonus(12) < attribute_bonus(17));
    REQUIRE(attribute_bonus(25) > attribute_bonus(21));
    // Thirteen is the pivot, and the steps above it are the table's own.
    REQUIRE(attribute_bonus(13) == 0);
    REQUIRE(attribute_bonus(15) == 1);
    REQUIRE(attribute_bonus(17) == 2);
    REQUIRE(attribute_bonus(19) == 3);
    REQUIRE(attribute_bonus(21) == 4);
    REQUIRE(attribute_bonus(25) == 5);
    REQUIRE(attribute_bonus(100) == 11);
    // Below it the ladder falls to a floor of minus six, and stops.
    REQUIRE(attribute_bonus(11) == -1);
    REQUIRE(attribute_bonus(9) == -2);
    REQUIRE(attribute_bonus(3) == -5);
    REQUIRE(attribute_bonus(1) == -6);
    REQUIRE(attribute_bonus(0) == -6);
    // And it tops out at thirty, however high the value climbs.
    REQUIRE(attribute_bonus(500) == 30);
    REQUIRE(attribute_bonus(5000) == 30);
    // The two tables are the same length and the ladder never rises.
    REQUIRE(kAttributeLadder.size() == kAttributeBonus.size());
    for (std::size_t i = 1; i < kAttributeLadder.size(); ++i) {
        REQUIRE(kAttributeLadder[i] < kAttributeLadder[i - 1]);
        REQUIRE(kAttributeBonus[i] < kAttributeBonus[i - 1]);
    }
}

TEST_CASE("a starting party is four named characters", "[party]") {
    const auto party = make_party(names(), 7);
    REQUIRE(party.size() == 4);
    for (const auto& who : party) {
        REQUIRE_FALSE(who.name.empty());
        REQUIRE_FALSE(who.class_name.empty());
        REQUIRE(who.face >= 0);
        REQUIRE(who.face < kFaceCount);
        REQUIRE(who.hit_points > 0);
        REQUIRE(who.hit_points == who.max_hit_points);
    }
    REQUIRE(party[0].class_name == std::string(kStartingClasses[0]));
    REQUIRE(party[3].class_name == std::string(kStartingClasses[3]));
}

TEST_CASE("a name comes from the column its face belongs to", "[party]") {
    // The name table has one column per sex and the portraits are eight male
    // then four female, so the two have to agree.
    const auto table = names();
    const auto party = make_party(table, 11);
    for (const auto& who : party) {
        const auto& column = face_is_female(who.face) ? table.female() : table.male();
        REQUIRE(std::find(column.begin(), column.end(), who.name) != column.end());
    }
}

TEST_CASE("the same seed makes the same party", "[party]") {
    // A party that changes between runs makes every screenshot and every bug
    // report a different game.
    const auto table = names();
    const auto first = make_party(table, 42);
    const auto second = make_party(table, 42);
    const auto other = make_party(table, 43);
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].name == second[i].name);
        REQUIRE(first[i].face == second[i].face);
        REQUIRE(first[i].attributes == second[i].attributes);
    }
    REQUIRE(first[0].attributes != other[0].attributes);
}

TEST_CASE("only the spellcasters have spell points", "[party]") {
    const auto party = make_party(names(), 3);
    for (const auto& who : party) {
        if (casts_spells(who.class_name)) {
            REQUIRE(who.max_spell_points > 0);
        } else {
            REQUIRE(who.max_spell_points == 0);
        }
    }
}

TEST_CASE("the sheet's field names come from the table, not from here", "[party]") {
    // stats.txt lists the seven attributes first, in the order the character
    // struct stores them.
    std::string body = "Stats Descriptions\tDescription\r\n";
    for (const char* name : {"Might", "Intellect", "Personality", "Endurance", "Accuracy", "Speed",
                             "Luck", "Hit Points"}) {
        body += std::string(name) + "\tsomething\r\n";
    }
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::DescriptionTable stats;
    data::DescriptionTable::parse(table, stats);

    REQUIRE(stat_label(stats, static_cast<std::size_t>(Attribute::Might)) == "Might");
    REQUIRE(stat_label(stats, static_cast<std::size_t>(Attribute::Luck)) == "Luck");
    REQUIRE(stat_label(stats, kAttributeCount) == "Hit Points");
    REQUIRE(stat_label(stats, 99).empty());
}

TEST_CASE("the conditions hold a character back by name", "[party]") {
    Character who;
    who.hit_points = 10;
    REQUIRE(who.can_act());
    who.affliction = "Asleep";
    REQUIRE_FALSE(who.can_act());
    who.affliction = "Affraid";  // the table's own spelling
    REQUIRE(who.can_act());
    REQUIRE(who.afraid());
    who.affliction = "Dead";
    REQUIRE(who.dead());
    REQUIRE_FALSE(who.can_act());
    who.affliction.clear();
    who.hit_points = 0;
    REQUIRE_FALSE(who.can_act());
    REQUIRE_FALSE(who.dead());
}

TEST_CASE("a night clears what a night can", "[party]") {
    Character who;
    who.affliction = "Affraid";
    who.rest_expires();
    REQUIRE(who.affliction.empty());
    who.affliction = "Curse";
    who.rest_expires();
    REQUIRE(who.affliction == "Curse");
}

TEST_CASE("rest wakes the unconscious but not the dead", "[party]") {
    std::array<Character, 4> party{};
    for (auto& who : party) {
        who.max_hit_points = 20;
        who.hit_points = 15;
    }
    party[1].hit_points = 0;               // knocked out
    party[2].hit_points = 0;
    party[2].affliction = "Dead";
    game::GameClock clock;
    REQUIRE(game::rest(party, clock, false) == game::RestResult::Rested);
    REQUIRE(party[0].hit_points == 20);
    REQUIRE(party[1].hit_points == 1);     // comes to, barely
    REQUIRE(party[2].hit_points == 0);     // the night does nothing for them
    REQUIRE(party[2].dead());
}

TEST_CASE("reshaping a character re-derives what the class decides", "[party]") {
    Character who;
    who.class_name = "Knight";
    Mm6Random random{7};
    roll_attributes(who, random);
    for (std::size_t a = 0; a < kAttributeCount; ++a) {
        REQUIRE(who.attributes[a] >= 11);
        REQUIRE(who.attributes[a] <= 20);
    }
    derive_start(who);
    REQUIRE(who.max_hit_points > 0);
    REQUIRE(who.max_spell_points == 0);
    REQUIRE(who.known_spells.empty());

    // The same character turned Cleric learns to cast; turned back, forgets.
    who.class_name = "Cleric";
    derive_start(who);
    REQUIRE(who.max_spell_points > 0);
    REQUIRE(who.known_spells.contains(68));
    who.class_name = "Knight";
    derive_start(who);
    REQUIRE(who.max_spell_points == 0);
    REQUIRE(who.known_spells.empty());
}

TEST_CASE("the base classes are six and every one is a Class.txt heading shape", "[party]") {
    REQUIRE(kBaseClasses.size() == 6);
    for (const auto name : kBaseClasses) {
        REQUIRE_FALSE(name.empty());
    }
    // Every starting-party class is one of them.
    for (const auto name : kStartingClasses) {
        REQUIRE(std::find(kBaseClasses.begin(), kBaseClasses.end(), name) !=
                kBaseClasses.end());
    }
}

TEST_CASE("a class is worth its own hit points and spell points", "[party]") {
    // The eighteen are the executable's own order, three to a family.
    REQUIRE(kClassNames.size() == 18);
    REQUIRE(class_id("Knight") == 0);
    REQUIRE(class_id("Champion") == 2);
    REQUIRE(class_id("Cleric") == 3);
    REQUIRE(class_id("Arch Druid") == 17);
    // Class.txt's own words: a Cavalier gets two hit points a level more
    // than a Knight.
    REQUIRE(kClassHitPointsPerLevel[class_id("Cavalier")] -
                kClassHitPointsPerLevel[class_id("Knight")] ==
            2);
    // The bases run by family, and the fighters start hardiest.
    REQUIRE(kClassBaseHitPoints[0] == 30);
    REQUIRE(kClassBaseHitPoints[1] == 20);
    REQUIRE(class_hit_points("Knight", 1, 0) > class_hit_points("Cleric", 1, 0));
    REQUIRE(class_hit_points("Champion", 1, 0) > class_hit_points("Knight", 1, 0));
    // A level and a point of Endurance bonus are worth the same thing.
    REQUIRE(class_hit_points("Knight", 2, 0) == class_hit_points("Knight", 1, 1));
    // Knights cast nothing at all; the pure casters get the most.
    REQUIRE(class_spell_points("Knight", 1, 0) == 0);
    REQUIRE(class_spell_points("Champion", 9, 9) == 0);
    REQUIRE(class_spell_points("Sorcerer", 1, 0) > class_spell_points("Paladin", 1, 0));
    REQUIRE(class_spell_points("Archmage", 1, 0) > class_spell_points("Sorcerer", 1, 0));
    // And an unknown name falls to the first class rather than reading past
    // the tables.
    REQUIRE(class_id("Nonesuch") == 0);
    REQUIRE(class_hit_points("Nonesuch", 1, 0) == class_hit_points("Knight", 1, 0));
}

TEST_CASE("the starting four are no longer interchangeable", "[party]") {
    const auto party = make_party(names(), 7);
    REQUIRE(party[0].max_hit_points > party[3].max_hit_points);  // Knight over Cleric
    REQUIRE(party[0].max_spell_points == 0);
    REQUIRE(party[3].max_spell_points > party[1].max_spell_points);  // Cleric over Paladin
    for (const auto& who : party) {
        REQUIRE(who.max_hit_points >= 1);
        REQUIRE(who.hit_points == who.max_hit_points);
        REQUIRE(who.spell_points == who.max_spell_points);
    }
}

TEST_CASE("experience buys levels, and levels buy the class's own numbers", "[party]") {
    REQUIRE(experience_for_level(1) == 0);
    REQUIRE(experience_for_level(2) == 1000);
    REQUIRE(experience_for_level(3) == 3000);
    REQUIRE(level_for_experience(0) == 1);
    REQUIRE(level_for_experience(999) == 1);
    REQUIRE(level_for_experience(1000) == 2);
    REQUIRE(level_for_experience(3000) == 3);
    // A Knight gains exactly what its row says, and a Cleric its own.
    auto knight = make_party(names(), 7)[0];
    const int before = knight.max_hit_points;
    knight.experience = 1000;
    REQUIRE(level_up(knight) == 1);
    REQUIRE(knight.level == 2);
    REQUIRE(knight.max_hit_points - before == kClassHitPointsPerLevel[class_id("Knight")]);
    // The wounds carried are kept: only the maxima moved, and the current
    // value rose with them.
    REQUIRE(knight.hit_points == knight.max_hit_points);
    // A second call with no new experience does nothing.
    REQUIRE(level_up(knight) == 0);
    // Several levels at once are taken in one step.
    auto cleric = make_party(names(), 7)[3];
    cleric.experience = 10000;
    REQUIRE(level_up(cleric) == level_for_experience(10000) - 1);
    REQUIRE(cleric.max_spell_points > 0);
}

TEST_CASE("what ails a character cuts its numbers", "[party]") {
    Character who;
    who.class_name = "Knight";
    who.hit_points = 10;
    who.attributes[static_cast<std::size_t>(Attribute::Speed)] = 40;
    // Well: nothing is cut.
    REQUIRE(condition_scale(who) == 100);
    REQUIRE(ailing_attribute(who, Attribute::Speed) == 40);
    // Poisoned takes a quarter, diseased two fifths.
    who.poisoned = 1;
    REQUIRE(worst_condition_of(who) == kConditionPoisoned);
    REQUIRE(condition_scale(who) == 75);
    REQUIRE(ailing_attribute(who, Attribute::Speed) == 30);
    who.diseased = 1;
    // Diseased outranks poisoned in the priority order, so it wins.
    REQUIRE(worst_condition_of(who) == kConditionDiseased);
    REQUIRE(condition_scale(who) == 60);
    // And death outranks everything.
    who.hit_points = 0;
    REQUIRE(worst_condition_of(who) == kConditionDead);
    // A cut attribute is a smaller bonus, which is how it reaches the class
    // tables and the recovery.
    Character hale = who;
    hale.hit_points = 10;
    hale.poisoned = 0;
    hale.diseased = 0;
    REQUIRE(attribute_bonus(ailing_attribute(hale, Attribute::Speed)) >
            attribute_bonus(30));
}
