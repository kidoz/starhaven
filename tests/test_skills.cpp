// Tests for reading SKILLDES.TXT's effect lines and the engine's own rules
// around them.
#include <catch2/catch_test_macros.hpp>

#include "game/skills.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("a skill's effect lines say what it grants", "[skills]") {
    // The table's own phrasings, one per shipped kind.
    const auto axe = parse_skill_effect({"Skill added to Attack Bonus",
                                         "Skill reduces recovery time",
                                         "Skill added to Attack Damage"});
    REQUIRE(axe.attack_bonus);
    REQUIRE(axe.attack_damage);
    REQUIRE_FALSE(axe.armor_class);

    const auto shield = parse_skill_effect({"Skill added to Armor Class",
                                            "Skill added to Armor Class (double effect)",
                                            "Skill added to Armor Class (triple effect)"});
    REQUIRE(shield.armor_class);

    const auto merchant = parse_skill_effect({"Skill adjusts shop prices in your favor",
                                              "Double effect of skill", "Triple effect of skill"});
    REQUIRE(merchant.shop_prices);

    const auto fire = parse_skill_effect(
        {"Effects vary per spell", "Effects vary per spell", "Effects vary per spell"});
    REQUIRE_FALSE(fire.attack_bonus);
    REQUIRE_FALSE(fire.shop_prices);
}

TEST_CASE("the staircase and the haggle behave", "[skills]") {
    REQUIRE(raise_cost(1) == 2);
    REQUIRE(raise_cost(4) == 5);
    // One percent a point, floored at half price, never below one gold.
    REQUIRE(haggled_price(100, 0) == 100);
    REQUIRE(haggled_price(100, 10) == 90);
    REQUIRE(haggled_price(100, 80) == 50);
    REQUIRE(haggled_price(1, 50) == 1);
}

TEST_CASE("every base class starts with a weapon skill", "[skills]") {
    REQUIRE(starting_skill("Knight") == "Sword");
    REQUIRE(starting_skill("Archer") == "Bow");
    REQUIRE(starting_skill("Cleric") == "Mace");
    REQUIRE(starting_skill("Sorcerer") == "Staff");
}
