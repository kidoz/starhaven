// Tests for reading SKILLDES.TXT's effect lines and the engine's own rules
// around them.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <map>

#include "game/skills.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("the attack bonus mixes two attributes", "[skills]") {
    // The getter asks the bonus getters for one stat and reads the stored
    // pair of another, and sums them before the ladder.
    REQUIRE(traced_attack_bonus(0, 0) == 0);
    REQUIRE(traced_attack_bonus(5, 30) == 35);
    REQUIRE(traced_attack_bonus(0, 30) == 30);
    REQUIRE(traced_attack_bonus(5, 0) == 5);
    // Cutting the stored term cuts the total, which is how age and
    // condition reach the roll.
    REQUIRE(traced_attack_bonus(5, 30 * 75 / 100) == 27);
}


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

TEST_CASE("the recovery table answers by skill group", "[skills]") {
    // The executable's own fourteen words, indexed by skill id plus one.
    REQUIRE(kBareHandRecovery == 100);
    REQUIRE(gear_recovery("Staff") == 100);
    REQUIRE(gear_recovery("Sword") == 90);
    REQUIRE(gear_recovery("Dagger") == 60);
    REQUIRE(gear_recovery("Axe") == 100);
    REQUIRE(gear_recovery("Spear") == 80);
    REQUIRE(gear_recovery("Bow") == 100);
    REQUIRE(gear_recovery("Mace") == 80);
    REQUIRE(gear_recovery("Blaster") == 30);
    // Armour and the shield: the heavier the slower.
    REQUIRE(gear_recovery("Shield") == 10);
    REQUIRE(gear_recovery("Leather") == 10);
    REQUIRE(gear_recovery("Chain") == 20);
    REQUIRE(gear_recovery("Plate") == 30);
    // A school is not gear and carries nothing.
    REQUIRE(gear_recovery("Fire") == 0);
    REQUIRE(gear_recovery("") == 0);
    // The higher lines take the worn penalty back by half, then whole.
    REQUIRE(worn_recovery_penalty(30, 0) == 30);
    REQUIRE(worn_recovery_penalty(30, 1) == 15);
    REQUIRE(worn_recovery_penalty(30, 2) == 0);
}

TEST_CASE("the staircase and the haggle behave", "[skills]") {
    REQUIRE(raise_cost(1) == 2);
    REQUIRE(raise_cost(4) == 5);
    // The weights these once carried came from a table that turned out to
    // be about conditions, not skills, so what is left is the plain one
    // percent a point, floored at half price, never below a gold — all of
    // it this engine's own.
    REQUIRE(haggled_price(100, 0) == 100);
    REQUIRE(haggled_price(100, 10) == 90);
    REQUIRE(haggled_price(100, 50) == 50);
    REQUIRE(haggled_price(100, 500) == 50);
    REQUIRE(haggled_price(1, 50) == 1);
    REQUIRE(weighted_identify(10) == 10);
    REQUIRE(weighted_repair(10) == 10);
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

TEST_CASE("the left hand opens at the line's own rank", "[skills]") {
    const std::vector<std::string> dagger{"Skill added to Attack Bonus",
                                          "Permits use of dagger in left hand",
                                          "Chance to cause triple damage equal to skill"};
    REQUIRE_FALSE(skill_power(dagger, 3).left_hand);
    REQUIRE(skill_power(dagger, 4).left_hand);  // the expert line
    const std::vector<std::string> sword{"Skill added to Attack Bonus",
                                         "Skill reduces recovery time",
                                         "Permits use of sword in left hand"};
    REQUIRE_FALSE(skill_power(sword, 6).left_hand);
    REQUIRE(skill_power(sword, 7).left_hand);  // the master line
}

TEST_CASE("a group the table does not name costs nothing extra", "[skills]") {
    // ITEMS.TXT ships thirteen skill groups; "Club" is not one of the twelve
    // the recovery table covers, and reading its zero as a recovery would
    // make a club strike instantly.
    REQUIRE(gear_recovery("Club") == 0);
    REQUIRE(gear_recovery("Misc") == 0);
    // Which is why the caller keeps the bare-hand default when the lookup
    // comes back empty.
    const int held = gear_recovery("Club");
    const int spent = held > 0 ? held : kBareHandRecovery;
    REQUIRE(spent == kBareHandRecovery);
}

TEST_CASE("a skill byte packs points under a mastery", "[skills]") {
    REQUIRE(kSkillArrayOffset == 0x60);
    REQUIRE(kSkillSlots == 31);
    // Low six bits the points, top two the rung.
    REQUIRE(skill_points(0x0c) == 12);
    REQUIRE(skill_mastery(0x0c) == 0);
    REQUIRE(skill_points(0x4c) == 12);
    REQUIRE(skill_mastery(0x4c) == 1);
    REQUIRE(skill_mastery(0xcc) == 3);
    // Zero is not "novice at nothing", it is not learned.
    REQUIRE(skill_points(0) == 0);
}

TEST_CASE("training costs the point it buys", "[skills]") {
    REQUIRE(skill_raise_cost(0) == 1);
    REQUIRE(skill_raise_cost(11) == 12);
    int packed = 4;
    int pool = 10;
    REQUIRE(train_skill(packed, pool));
    REQUIRE(skill_points(packed) == 5);
    REQUIRE(pool == 5);  // five spent for the fifth point
    // The next point costs six and the pool is short, so nothing moves.
    REQUIRE_FALSE(train_skill(packed, pool));
    REQUIRE(skill_points(packed) == 5);
    REQUIRE(pool == 5);
    // The mastery bits ride along untouched.
    int master = 0x80 | 4;
    int purse = 99;
    REQUIRE(train_skill(master, purse));
    REQUIRE(skill_mastery(master) == 2);
    REQUIRE(skill_points(master) == 5);
    // An unlearned skill cannot be trained into existence.
    int absent = 0;
    REQUIRE_FALSE(train_skill(absent, purse));
    // And the ceiling holds at sixty.
    int capped = kSkillPointCap;
    REQUIRE_FALSE(train_skill(capped, purse));
    int last = kSkillPointCap - 1;
    REQUIRE(train_skill(last, purse));
    REQUIRE(skill_points(last) == kSkillPointCap);
}

TEST_CASE("a made character owes four skills", "[skills]") {
    REQUIRE(kStartingSkillsRequired == 4);
    std::array<int, kSkillSlots> slots{};
    const auto read = [&slots](int slot) { return slots[static_cast<std::size_t>(slot)]; };
    REQUIRE_FALSE(character_skills_chosen(read));
    slots[1] = 1;
    slots[4] = 1;
    slots[9] = 1;
    REQUIRE_FALSE(character_skills_chosen(read));
    slots[30] = 1;  // the last slot the walk reaches counts
    REQUIRE(character_skills_chosen(read));
}
