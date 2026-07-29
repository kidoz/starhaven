// Tests for the promotion join: awards name classes, classes state gains.
#include <catch2/catch_test_macros.hpp>

#include "core/data/spell_stats.hpp"
#include "game/promotion.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

// Class.txt's shape: six ladders of three, in row order. Two are enough.
data::DescriptionTable ladders() {
    std::string body = "\r\nSkill\tDescription\r\n";
    body += "Knight\tThe Knight class is the workhorse fighting class.\r\n";
    body +=
        "Cavalier\tCavaliers enjoy the benefit of an extra two hit points per level, and can "
        "be promoted once more to Champion status.\r\n";
    body += "Champion\tChampions enjoy the benefit of an extra four hit points per level.\r\n";
    body += "Cleric\tClerics are adventuring, spell casting holy men.\r\n";
    body +=
        "Priest\tPriests enjoy the benefit of an extra hit point and spell point per level.\r\n";
    body +=
        "High Priest\tHigh Priests enjoy the benefit of an extra two hit points and spell "
        "points per level.\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::DescriptionTable out;
    data::DescriptionTable::parse(table, out);
    REQUIRE(out.size() == 6);
    return out;
}

}  // namespace

TEST_CASE("a class's own prose states its per-level worth", "[promotion]") {
    REQUIRE(parse_class_gains("enjoy the benefit of an extra two hit points per level")
                .hp_per_level == 2);
    REQUIRE(parse_class_gains("enjoy the benefit of an extra four hit points per level")
                .hp_per_level == 4);
    const auto priest =
        parse_class_gains("enjoy the benefit of an extra hit point and spell point per level");
    REQUIRE(priest.hp_per_level == 1);
    REQUIRE(priest.sp_per_level == 1);
    const auto high =
        parse_class_gains("an extra two hit points and spell points per level");
    REQUIRE(high.hp_per_level == 0);  // needs the full "benefit of" anchor
    REQUIRE(parse_class_gains("The Knight class is the workhorse fighting class.")
                .hp_per_level == 0);
}

TEST_CASE("an award names its class, honorary names none", "[promotion]") {
    REQUIRE(promotion_of("Received Promotion to Crusader") == "Crusader");
    REQUIRE(promotion_of("Received Promotion to Archmage") == "Archmage");
    REQUIRE(promotion_of("Received Promotion to Honorary Crusader").empty());
    REQUIRE(promotion_of("Solved the Goblinwatch Combination").empty());
}

TEST_CASE("only the same ladder promotes, and only upward", "[promotion]") {
    const auto classes = ladders();
    REQUIRE(promotes_to(classes, "Knight", "Cavalier"));
    REQUIRE(promotes_to(classes, "Knight", "Champion"));
    REQUIRE(promotes_to(classes, "Cavalier", "Champion"));
    REQUIRE_FALSE(promotes_to(classes, "Champion", "Cavalier"));
    REQUIRE_FALSE(promotes_to(classes, "Knight", "Priest"));
    // The award's spelling and the table's may differ by spaces and case.
    REQUIRE(promotes_to(classes, "Cleric", "high priest"));
}

TEST_CASE("promotion pays the prose's gains for the levels held", "[promotion]") {
    const auto classes = ladders();
    Character who;
    who.class_name = "Knight";
    who.level = 10;
    who.max_hit_points = 70;
    who.hit_points = 70;
    promote(who, classes, "Cavalier");
    REQUIRE(who.class_name == "Cavalier");
    REQUIRE(who.max_hit_points == 90);  // +2 x 10 levels

    // Stepping on to Champion pays only the difference: +2 more a level.
    promote(who, classes, "Champion");
    REQUIRE(who.max_hit_points == 110);

    // A priest's promotion opens spell points even on a late start.
    Character cleric;
    cleric.class_name = "Cleric";
    cleric.level = 4;
    cleric.max_hit_points = 30;
    cleric.hit_points = 30;
    cleric.max_spell_points = 20;
    cleric.spell_points = 20;
    promote(cleric, classes, "High Priest");
    REQUIRE(cleric.max_hit_points == 38);
    REQUIRE(cleric.max_spell_points == 28);
}
