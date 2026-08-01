// Tests for reading SKILLDES.TXT's effect lines and the engine's own rules
// around them.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <map>

#include "game/skills.hpp"
#include "game/special_stats.hpp"

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
    REQUIRE(kSkillNames[static_cast<std::size_t>(class_starting_skills(0)[0])] == "Sword");
    REQUIRE(kSkillNames[static_cast<std::size_t>(class_starting_skills(12)[0])] == "Bow");
    REQUIRE(kSkillNames[static_cast<std::size_t>(class_starting_skills(3)[0])] == "Mace");
    // Two of the six guesses this replaces were wrong.
    REQUIRE(kSkillNames[static_cast<std::size_t>(class_starting_skills(6)[0])] == "Dagger");
    REQUIRE(kSkillNames[static_cast<std::size_t>(class_starting_skills(9)[0])] == "Sword");
}

TEST_CASE("the higher lines wake at their rank bits", "[skills]") {
    const std::vector<std::string> mace{"the prose column", "Skill added to Attack Bonus",
                                        "Skill added to Attack Damage",
                                        "Chance to stun equal to skill"};
    // Three points: normal only — bonus but no damage, no stun.
    auto low = skill_power(mace, 3);
    REQUIRE(low.to_hit == 3);
    REQUIRE(low.damage == 0);
    REQUIRE(low.stun_percent == 0);
    // Five points: expert — damage joins.
    auto mid = skill_power(mace, 5 | 0x40);  // expert by its bit, not its points
    REQUIRE(mid.damage == 5);
    REQUIRE(mid.stun_percent == 0);
    // Eight points: master — the stun equals the skill.
    auto high = skill_power(mace, 8 | 0x80);
    REQUIRE(high.stun_percent == 8);

    const std::vector<std::string> shield{"the prose column", "Skill added to Armor Class",
                                          "Skill added to Armor Class (double effect)",
                                          "Skill added to Armor Class (triple effect)"};
    REQUIRE(skill_power(shield, 3).armor == 3);
    REQUIRE(skill_power(shield, 4 | 0x40).armor == 8);
    REQUIRE(skill_power(shield, 7 | 0x80).armor == 21);

    const std::vector<std::string> bow{"the prose column", "Skill added to Attack Bonus",
                                       "Skill reduces recovery time",
                                       "Bow fires two arrows on every attack"};
    REQUIRE_FALSE(skill_power(bow, 6 | 0x40).second_arrow);
    REQUIRE(skill_power(bow, 7 | 0x80).second_arrow);
    REQUIRE(skill_power(bow, 6 | 0x40).recovery_scale < 1.0f);
    REQUIRE(skill_power(bow, 3).recovery_scale == 1.0f);

    const std::vector<std::string> dagger{"the prose column", "Skill added to Attack Bonus",
                                          "Permits use of dagger in left hand",
                                          "Chance to cause triple damage equal to skill"};
    REQUIRE(skill_power(dagger, 7 | 0x80).triple_percent == 7);
    REQUIRE(skill_power(dagger, 6 | 0x40).triple_percent == 0);

    const std::vector<std::string> merchant{"the prose column", "Skill adjusts shop prices in your favor",
                                            "Double effect of skill", "Triple effect of skill"};
    REQUIRE(skill_power(merchant, 3).price_percent == 3);
    REQUIRE(skill_power(merchant, 4 | 0x40).price_percent == 8);
    REQUIRE(skill_power(merchant, 7 | 0x80).price_percent == 21);
}

TEST_CASE("the body's lines grant points and lift the armor's drag", "[skills]") {
    const std::vector<std::string> body{"the prose column", "Skill adds to Hit Points", "Double effect of skill",
                                        "Triple effect of skill"};
    REQUIRE(skill_power(body, 3).hp_bonus == 3);
    REQUIRE(skill_power(body, 4 | 0x40).hp_bonus == 8);
    REQUIRE(skill_power(body, 7 | 0x80).hp_bonus == 21);
    const std::vector<std::string> meditation{"the prose column", "Skill adds to Spell Points",
                                              "Double effect of skill",
                                              "Triple effect of skill"};
    REQUIRE(skill_power(meditation, 5 | 0x40).sp_bonus == 10);

    const std::vector<std::string> plate{"the prose column", "Skill added to Armor Class",
                                         "Recovery penalty reduced",
                                         "Recovery penalty eliminated"};
    REQUIRE(skill_power(plate, 3).armor_penalty_lift == 0);
    REQUIRE(skill_power(plate, 4 | 0x40).armor_penalty_lift == 1);
    REQUIRE(skill_power(plate, 7 | 0x80).armor_penalty_lift == 2);
    REQUIRE(armor_penalty("Plate") > armor_penalty("Chain"));
    REQUIRE(armor_penalty("Chain") > armor_penalty("Leather"));
    REQUIRE(armor_penalty("Sword") == 0.0f);
}

TEST_CASE("the left hand opens at the line's own rank", "[skills]") {
    const std::vector<std::string> dagger{"the prose column", "Skill added to Attack Bonus",
                                          "Permits use of dagger in left hand",
                                          "Chance to cause triple damage equal to skill"};
    REQUIRE_FALSE(skill_power(dagger, 3).left_hand);
    REQUIRE(skill_power(dagger, 4 | 0x40).left_hand);  // the expert line
    const std::vector<std::string> sword{"the prose column", "Skill added to Attack Bonus",
                                         "Skill reduces recovery time",
                                         "Permits use of sword in left hand"};
    REQUIRE_FALSE(skill_power(sword, 6 | 0x40).left_hand);
    REQUIRE(skill_power(sword, 7 | 0x80).left_hand);  // the master line
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

TEST_CASE("the thirty-one slots are SKILLDES.TXT's own rows", "[skills]") {
    REQUIRE(kSkillNames.size() == static_cast<std::size_t>(kSkillSlots));
    REQUIRE(kSkillNames[0] == "Staff");
    REQUIRE(kSkillNames[12] == "Fire");
    REQUIRE(kSkillNames[30] == "Learning");
    REQUIRE(skill_id("Bow") == 5);
    REQUIRE(skill_id("Meditation") == 25);
    REQUIRE(skill_id("Nothing At All") == -1);
}

TEST_CASE("each class begins with the pair its row marks", "[skills]") {
    // The families are the eighteen classes over three, and the two ones in
    // each row are the skills that class starts play with.
    REQUIRE(class_family(0) == 0);   // Knight
    REQUIRE(class_family(2) == 0);   // Champion is still a Knight
    REQUIRE(class_family(17) == 5);  // Arch Druid is a Druid
    const std::array<std::pair<int, std::pair<const char*, const char*>>, 6> starts{{
        {0, {"Sword", "Leather"}},
        {3, {"Mace", "Body"}},
        {6, {"Dagger", "Fire"}},
        {9, {"Sword", "Spirit"}},
        {12, {"Bow", "Air"}},
        {15, {"Staff", "Earth"}},
    }};
    for (const auto& [who, pair] : starts) {
        const auto got = class_starting_skills(who);
        REQUIRE(got[0] == skill_id(pair.first));
        REQUIRE(got[1] == skill_id(pair.second));
    }
}

TEST_CASE("a class may never learn what its row zeroes", "[skills]") {
    // A knight has no magic at all, and no meditation to feed it.
    for (int school = skill_id("Fire"); school <= skill_id("Dark"); ++school) {
        REQUIRE_FALSE(class_may_learn(0, school));
    }
    REQUIRE_FALSE(class_may_learn(0, skill_id("Meditation")));
    REQUIRE(class_may_learn(0, skill_id("Plate")));
    // A sorcerer has the four elements and none of the three self schools.
    REQUIRE(class_may_learn(6, skill_id("Fire")));
    REQUIRE(class_may_learn(6, skill_id("Earth")));
    REQUIRE_FALSE(class_may_learn(6, skill_id("Spirit")));
    REQUIRE_FALSE(class_may_learn(6, skill_id("Plate")));
    REQUIRE_FALSE(class_may_learn(6, skill_id("Shield")));
    // A druid takes no bladed weapon bigger than a dagger, and no mail.
    REQUIRE_FALSE(class_may_learn(15, skill_id("Sword")));
    REQUIRE_FALSE(class_may_learn(15, skill_id("Axe")));
    REQUIRE_FALSE(class_may_learn(15, skill_id("Chain")));
    // Thievery is the one slot no class in the game may touch.
    for (int who = 0; who < 18; who += 3) {
        REQUIRE_FALSE(class_may_learn(who, skill_id("Thievery")));
    }
    // And out-of-range asks are refused rather than read past the table.
    REQUIRE_FALSE(class_may_learn(0, -1));
    REQUIRE_FALSE(class_may_learn(0, kSkillSlots));
}

TEST_CASE("the offered list is what a new character picks from", "[skills]") {
    // Two granted, some offered, the rest for a trainer later or never.
    int granted = 0;
    int offered = 0;
    for (int slot = 0; slot < kSkillSlots; ++slot) {
        const auto access = class_skill_access(3, slot);  // Cleric
        granted += access == SkillAccess::Granted ? 1 : 0;
        offered += access == SkillAccess::Offered ? 1 : 0;
    }
    REQUIRE(granted == 2);
    REQUIRE(offered > 0);
    REQUIRE(class_skill_access(3, skill_id("Mace")) == SkillAccess::Granted);
    REQUIRE(class_skill_access(3, skill_id("Light")) == SkillAccess::Later);
    REQUIRE(class_skill_access(3, skill_id("Plate")) == SkillAccess::Never);
}

TEST_CASE("a class begins with two skills, not one", "[skills]") {
    // The retraction this replaces gave one invented weapon skill per class.
    for (int who = 0; who < 18; who += 3) {
        const auto pair = class_starting_skills(who);
        REQUIRE(pair[0] >= 0);
        REQUIRE(pair[1] >= 0);
        REQUIRE(pair[0] != pair[1]);
        // Both are granted, and no third slot is.
        REQUIRE(class_skill_access(who, pair[0]) == SkillAccess::Granted);
        REQUIRE(class_skill_access(who, pair[1]) == SkillAccess::Granted);
        int granted = 0;
        for (int slot = 0; slot < kSkillSlots; ++slot) {
            granted += class_skill_access(who, slot) == SkillAccess::Granted ? 1 : 0;
        }
        REQUIRE(granted == 2);
    }
}

TEST_CASE("the rank lives in the byte, not in the point count", "[skills]") {
    // The retraction this replaces made expert begin at four points and
    // master at seven. Neither number is in the game.
    REQUIRE(skill_rank(3) == 0);
    REQUIRE(skill_rank(30) == 0);   // thirty points and still a novice
    REQUIRE(skill_rank(0x42) == 1);  // two points and an expert
    REQUIRE(skill_rank(0x81) == 2);
    REQUIRE(skill_points(0x81) == 1);
    REQUIRE(kRankNames[0] == "Normal");
    REQUIRE(kRankNames[2] == "Master");
    // Teaching sets the bits and leaves the points alone.
    const int taught = teach_rank(11, 2);
    REQUIRE(skill_points(taught) == 11);
    REQUIRE(skill_rank(taught) == 2);
    REQUIRE(teach_rank(taught, 0) == 11);
    // And training on top of a rank keeps it.
    int packed = teach_rank(11, 1);
    int pool = 99;
    REQUIRE(train_skill(packed, pool));
    REQUIRE(skill_points(packed) == 12);
    REQUIRE(skill_rank(packed) == 1);
}

TEST_CASE("a rank is bought from a teacher, at the teacher's price", "[skills]") {
    REQUIRE(kExpertPrice == 2000);
    REQUIRE(kMasterPrice == 5000);
    REQUIRE(teach_price(1) == kExpertPrice);
    REQUIRE(teach_price(2) == kMasterPrice);
    // The teacher clears the bits before setting one, so no byte ever carries
    // both: a master taught back down to expert is 0x40, not 0xc0.
    int packed = teach_rank(9, 2);
    REQUIRE(packed == (9 | 0x80));
    packed = teach_rank(packed, 1);
    REQUIRE(packed == (9 | 0x40));
    // And there is no fourth rung to ask for.
    REQUIRE(teach_rank(9, 3) == (9 | 0x80));
    REQUIRE(skill_rank(teach_rank(9, 3)) == 2);
    // The points are untouched throughout.
    for (int rank = 0; rank <= 3; ++rank) {
        REQUIRE(skill_points(teach_rank(37, rank)) == 37);
    }
}

TEST_CASE("the prose column is not an effect line", "[skills]") {
    // The row arrives as DescriptionTable hands it over: prose first, then
    // normal, expert and master. Reading from the prose shifted every rank
    // down by one and nothing above novice ever did what it says.
    const std::vector<std::string> bow{"Bow skill covers both bow and crossbow usage.",
                                       "Skill added to Attack Bonus",
                                       "Skill reduces recovery time",
                                       "Bow fires two arrows on every attack"};
    REQUIRE(skill_power(bow, 5).to_hit == 5);
    REQUIRE_FALSE(skill_power(bow, 5).second_arrow);
    REQUIRE(skill_power(bow, 5).recovery_scale == 1.0f);
    // Expert reaches the second line and no further.
    REQUIRE(skill_power(bow, 5 | 0x40).recovery_scale < 1.0f);
    REQUIRE_FALSE(skill_power(bow, 5 | 0x40).second_arrow);
    // Master reaches the third.
    REQUIRE(skill_power(bow, 5 | 0x80).second_arrow);
    // A row with the prose alone grants nothing at any rank.
    const std::vector<std::string> bare{"only prose here"};
    REQUIRE(skill_power(bare, 9 | 0x80).to_hit == 0);
}

TEST_CASE("the bare doublings are what a rank is worth to the silent rows",
          "[skills]") {
    // Sixteen of the thirty-one rows say nothing skill_power can parse: prose
    // at normal, then the bare "Double effect of skill" and "Triple effect of
    // skill". That much is readable and it is the whole of their rungs.
    REQUIRE(rank_multiplier(9) == 1);
    REQUIRE(rank_multiplier(teach_rank(9, 1)) == 2);
    REQUIRE(rank_multiplier(teach_rank(9, 2)) == 3);
    REQUIRE(weighted_identify(9) == 9);
    REQUIRE(weighted_identify(teach_rank(9, 1)) == 18);
    REQUIRE(weighted_repair(teach_rank(9, 2)) == 27);
    // The packed byte is never taken for a point count.
    REQUIRE(weighted_repair(teach_rank(2, 1)) == 4);
    REQUIRE(weighted_repair(0) == 0);
}

TEST_CASE("the stat id space is twenty-three ids with one gap", "[skills]") {
    // Every push before a stat getter, counted: 0..21 and 23.
    REQUIRE(kStatIdCount == 24);
    REQUIRE(kStatIdNeverAsked == 22);
    // The five resistances are not a contiguous run.
    REQUIRE(kResistanceStatIds.size() == 5);
    REQUIRE(kResistanceStatIds[3] == 13);
    REQUIRE(kResistanceStatIds[4] == 23);
    REQUIRE(kResistanceStatIds[4] != kResistanceStatIds[3] + 1);
    // The four that carry a stored term sit among the derived figures.
    REQUIRE(kStoredTermStatIds.size() == 4);
    for (const int id : kStoredTermStatIds) {
        REQUIRE(id > static_cast<int>(StatId::ArmorClass));
        REQUIRE(id < kStatIdNeverAsked);
    }
    // And the three named ones are where the getters put them.
    REQUIRE(static_cast<int>(StatId::HitPoints) == 7);
    REQUIRE(static_cast<int>(StatId::SpellPoints) == 8);
    REQUIRE(static_cast<int>(StatId::ArmorClass) == 9);
}

TEST_CASE("the four unnamed ids say where they take their number from",
          "[skills]") {
    // Anchors: sixteen from +0x1428, the weapon's at +0x142c, the next at
    // +0x1430 — which is what makes 17 and 18 the wielded weapon's numbers
    // and 21 the hand beside it.
    REQUIRE(kGearAnchorCount == 16);
    REQUIRE(kWeaponAnchor == kGearAnchorsFirst + 4);
    REQUIRE(kOffHandAnchor == kWeaponAnchor + 4);
    // Id 14 counts one special over everything worn, at five apiece.
    REQUIRE(kCountedSpecial == 25);
    REQUIRE(kCountedSpecialWorth == 5);
    // And that special is one of the thirty-nine the stat walk itself does
    // nothing with, which is why it needed a case of its own.
    REQUIRE_FALSE(special_reaches_stats(kCountedSpecial));
    REQUIRE(kBowSkillGroup == skill_id("Bow"));
}
