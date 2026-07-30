// Tests for the chests' traps: the original's own check, traced from the
// executable's chest-open path (docs/formats/event-tables.md).
#include <catch2/catch_test_macros.hpp>

#include "game/traps.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("the disarm value multiplies the original's way", "[traps]") {
    // Level times 2/3/4 by rank.
    REQUIRE(disarm_value(5, 0, 0, 0) == 10);
    REQUIRE(disarm_value(5, 1, 0, 0) == 15);
    REQUIRE(disarm_value(5, 2, 0, 0) == 20);
    // Each item doubling doubles the level, and they stack.
    REQUIRE(disarm_value(5, 0, 1, 0) == 20);
    REQUIRE(disarm_value(5, 0, 3, 0) == 80);
    // The hirelings' points join before the rank multiplies, so they
    // multiply too — and they are not doubled by items.
    REQUIRE(disarm_value(5, 2, 0, 18) == 92);
    REQUIRE(disarm_value(5, 0, 1, 4) == 28);
}

TEST_CASE("gear grants its doublings, broken gear grants nothing", "[traps]") {
    Character who;
    who.skills["Disarm Traps"] = 5;

    REQUIRE(disarm_item_doublings(who) == 0);
    // Five points is already expert by this engine's thresholds: 5 × 3.
    REQUIRE(character_disarm_value(who, 0) == 15);

    who.equipped[static_cast<std::size_t>(Slot::Cloak)] = kPendragonId;
    REQUIRE(disarm_item_doublings(who) == 1);

    who.equipped[static_cast<std::size_t>(Slot::Weapon)] = kHadesId;
    REQUIRE(disarm_item_doublings(who) == 2);

    // Anything worn "of Thievery" doubles once, however many pieces bear it.
    who.equipped[static_cast<std::size_t>(Slot::Ring)] = 100;
    who.worn_special[static_cast<std::size_t>(Slot::Ring)] = kOfThieverySpecial;
    who.equipped[static_cast<std::size_t>(Slot::Amulet)] = 101;
    who.worn_special[static_cast<std::size_t>(Slot::Amulet)] = kOfThieverySpecial;
    REQUIRE(disarm_item_doublings(who) == 3);
    REQUIRE(character_disarm_value(who, 0) == 120);  // (5 × 2³) × 3

    // A broken piece is mute.
    who.equipped_broken[static_cast<std::size_t>(Slot::Cloak)] = true;
    who.equipped_broken[static_cast<std::size_t>(Slot::Weapon)] = true;
    who.equipped_broken[static_cast<std::size_t>(Slot::Ring)] = true;
    REQUIRE(disarm_item_doublings(who) == 1);  // the amulet still counts

    // No skill, no value — gear multiplies nothing.
    who.skills.clear();
    REQUIRE(character_disarm_value(who, 8) == 0);
}

TEST_CASE("the roll beats five times the lock or does not", "[traps]") {
    // A value of zero never rolls.
    Mm6Random random{1};
    REQUIRE_FALSE(disarm_check(0, 0, random));

    // Any value beats a lockless map: value + d10 > 0.
    for (std::uint32_t seed = 0; seed < 20; ++seed) {
        Mm6Random r{seed};
        REQUIRE(disarm_check(1, 0, r));
    }
    // Certain failure: even a d10 of nine cannot reach. 41 + 9 = 50, not > 50.
    for (std::uint32_t seed = 0; seed < 20; ++seed) {
        Mm6Random r{seed};
        REQUIRE_FALSE(disarm_check(41, 10, r));
    }
    // Certain success: past the threshold before the die.
    for (std::uint32_t seed = 0; seed < 20; ++seed) {
        Mm6Random r{seed};
        REQUIRE(disarm_check(51, 10, r));
    }
    // On the boundary the die decides: both answers occur across seeds.
    bool passed = false, failed = false;
    for (std::uint32_t seed = 0; seed < 200 && !(passed && failed); ++seed) {
        Mm6Random r{seed};
        (disarm_check(50, 10, r) ? passed : failed) = true;
    }
    REQUIRE(passed);
    REQUIRE(failed);
}

TEST_CASE("the four elements all spring, and carry their names", "[traps]") {
    std::array<int, 4> seen{};
    Mm6Random random{7};
    for (int i = 0; i < 200; ++i) {
        ++seen[static_cast<std::size_t>(trap_element(random))];
    }
    for (const int count : seen) {
        REQUIRE(count > 0);
    }
    REQUIRE(trap_element_name(TrapElement::Fire) == "fire");
    REQUIRE(trap_element_name(TrapElement::Cold) == "cold");
    REQUIRE(trap_element_name(TrapElement::Electric) == "electric");
    REQUIRE(trap_element_name(TrapElement::Poison) == "poison");
}

TEST_CASE("the blast is five plus the trap column's d20s, rolled once", "[traps]") {
    // No dice, the flat five.
    Mm6Random none{3};
    REQUIRE(trap_damage(0, none) == 5);
    // Three dice stay within 5 + 3..60.
    for (std::uint32_t seed = 0; seed < 30; ++seed) {
        Mm6Random r{seed};
        const int rolled = trap_damage(3, r);
        REQUIRE(rolled >= 8);
        REQUIRE(rolled <= 65);
    }
    // The same seed rolls the same blast: it is one shared roll.
    Mm6Random a{11}, b{11};
    REQUIRE(trap_damage(9, a) == trap_damage(9, b));
}

TEST_CASE("the packed byte carries the level and the rank bits", "[traps]") {
    REQUIRE(packed_skill_byte(0) == 0);
    REQUIRE(packed_skill_byte(3) == 3);          // normal: bare points
    REQUIRE(packed_skill_byte(4) == 0x44);       // expert at four
    REQUIRE(packed_skill_byte(7) == 0x87);       // master at seven
}

TEST_CASE("perception leaps clear by the original's roll", "[traps]") {
    // No skill never dodges; neither does one point — rand % 21 cannot
    // clear 20.
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        Mm6Random r{seed};
        REQUIRE_FALSE(perception_dodges(0, r));
        REQUIRE_FALSE(perception_dodges(1, r));
    }
    // A master's packed byte dodges often, but not always: both outcomes
    // occur across seeds.
    bool dodged = false, caught = false;
    for (std::uint32_t seed = 0; seed < 200 && !(dodged && caught); ++seed) {
        Mm6Random r{seed};
        (perception_dodges(0x87, r) ? dodged : caught) = true;
    }
    REQUIRE(dodged);
    REQUIRE(caught);
}
