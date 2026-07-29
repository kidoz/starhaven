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

TEST_CASE("the higher lines wake at their ranks", "[skills]") {
    const std::vector<std::string> mace{"Skill added to Attack Bonus",
                                        "Skill added to Attack Damage",
                                        "Chance to stun equal to skill"};
    // Three points: normal only — bonus but no damage, no stun.
    auto low = skill_power(mace, 3);
    REQUIRE(low.to_hit == 3);
    REQUIRE(low.damage == 0);
    REQUIRE(low.stun_percent == 0);
    // Five points: expert — damage joins.
    auto mid = skill_power(mace, 5);
    REQUIRE(mid.damage == 5);
    REQUIRE(mid.stun_percent == 0);
    // Eight points: master — the stun equals the skill.
    auto high = skill_power(mace, 8);
    REQUIRE(high.stun_percent == 8);

    const std::vector<std::string> shield{"Skill added to Armor Class",
                                          "Skill added to Armor Class (double effect)",
                                          "Skill added to Armor Class (triple effect)"};
    REQUIRE(skill_power(shield, 3).armor == 3);
    REQUIRE(skill_power(shield, 4).armor == 8);
    REQUIRE(skill_power(shield, 7).armor == 21);

    const std::vector<std::string> bow{"Skill added to Attack Bonus",
                                       "Skill reduces recovery time",
                                       "Bow fires two arrows on every attack"};
    REQUIRE_FALSE(skill_power(bow, 6).second_arrow);
    REQUIRE(skill_power(bow, 7).second_arrow);
    REQUIRE(skill_power(bow, 6).recovery_scale < 1.0f);
    REQUIRE(skill_power(bow, 3).recovery_scale == 1.0f);

    const std::vector<std::string> dagger{"Skill added to Attack Bonus",
                                          "Permits use of dagger in left hand",
                                          "Chance to cause triple damage equal to skill"};
    REQUIRE(skill_power(dagger, 7).triple_percent == 7);
    REQUIRE(skill_power(dagger, 6).triple_percent == 0);

    const std::vector<std::string> merchant{"Skill adjusts shop prices in your favor",
                                            "Double effect of skill", "Triple effect of skill"};
    REQUIRE(skill_power(merchant, 3).price_percent == 3);
    REQUIRE(skill_power(merchant, 4).price_percent == 8);
    REQUIRE(skill_power(merchant, 7).price_percent == 21);
}

TEST_CASE("the body's lines grant points and lift the armor's drag", "[skills]") {
    const std::vector<std::string> body{"Skill adds to Hit Points", "Double effect of skill",
                                        "Triple effect of skill"};
    REQUIRE(skill_power(body, 3).hp_bonus == 3);
    REQUIRE(skill_power(body, 4).hp_bonus == 8);
    REQUIRE(skill_power(body, 7).hp_bonus == 21);
    const std::vector<std::string> meditation{"Skill adds to Spell Points",
                                              "Double effect of skill",
                                              "Triple effect of skill"};
    REQUIRE(skill_power(meditation, 5).sp_bonus == 10);

    const std::vector<std::string> plate{"Skill added to Armor Class",
                                         "Recovery penalty reduced",
                                         "Recovery penalty eliminated"};
    REQUIRE(skill_power(plate, 3).armor_penalty_lift == 0);
    REQUIRE(skill_power(plate, 4).armor_penalty_lift == 1);
    REQUIRE(skill_power(plate, 7).armor_penalty_lift == 2);
    REQUIRE(armor_penalty("Plate") > armor_penalty("Chain"));
    REQUIRE(armor_penalty("Chain") > armor_penalty("Leather"));
    REQUIRE(armor_penalty("Sword") == 0.0f);
}
