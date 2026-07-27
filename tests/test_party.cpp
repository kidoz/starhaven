// Tests for the party: portrait naming, attribute bonuses, and what a
// starting party is made of.
//
// Hermetic: the name table is built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/party.hpp"

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

TEST_CASE("the attribute bonus climbs with the attribute", "[party]") {
    REQUIRE(attribute_bonus(3) < attribute_bonus(7));
    REQUIRE(attribute_bonus(7) < attribute_bonus(12));
    REQUIRE(attribute_bonus(12) < attribute_bonus(17));
    REQUIRE(attribute_bonus(17) == 0);
    REQUIRE(attribute_bonus(25) > attribute_bonus(21));
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
