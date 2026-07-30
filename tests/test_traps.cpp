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

TEST_CASE("the blast rolls the lock's dice and spares the fallen", "[traps]") {
    std::array<Character, 4> party;
    for (auto& who : party) {
        who.hit_points = 30;
        who.max_hit_points = 30;
    }
    party[3].hit_points = 0;  // the fallen are past hurting

    Mm6Random random{11};
    const TrapBlast blast = spring_trap(5, party, random);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(blast.damage[i] >= 5);
        REQUIRE(blast.damage[i] <= 30);
    }
    REQUIRE(blast.damage[3] == 0);
}
