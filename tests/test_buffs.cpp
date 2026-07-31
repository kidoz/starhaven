// Tests for the party's spell-buff array, on the shape MM6.exe's own
// clearing routine gives it.
#include <catch2/catch_test_macros.hpp>

#include "game/buffs.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("the array is sixteen slots and a spell knows its own", "[buffs]") {
    REQUIRE(kPartyBuffCount == 16);
    // The five protections, by the ids their cases carry.
    REQUIRE(buff_slot_of_spell(3) == static_cast<int>(PartyBuff::ProtectionFromFire));
    REQUIRE(buff_slot_of_spell(25) == static_cast<int>(PartyBuff::ProtectionFromCold));
    REQUIRE(buff_slot_of_spell(14) == static_cast<int>(PartyBuff::ProtectionFromElectricity));
    REQUIRE(buff_slot_of_spell(36) == static_cast<int>(PartyBuff::ProtectionFromMagic));
    REQUIRE(buff_slot_of_spell(69) == static_cast<int>(PartyBuff::ProtectionFromPoison));
    // And the five others that own a slot.
    REQUIRE(buff_slot_of_spell(1) == static_cast<int>(PartyBuff::TorchLight));
    REQUIRE(buff_slot_of_spell(12) == static_cast<int>(PartyBuff::WizardEye));
    REQUIRE(buff_slot_of_spell(21) == static_cast<int>(PartyBuff::Fly));
    REQUIRE(buff_slot_of_spell(27) == static_cast<int>(PartyBuff::WaterWalk));
    REQUIRE(buff_slot_of_spell(50) == static_cast<int>(PartyBuff::GuardianAngel));
    // A damage spell owns nothing.
    REQUIRE(buff_slot_of_spell(2) == -1);
    REQUIRE(buff_slot_of_spell(0) == -1);
}

TEST_CASE("a slot holds its power until it lapses", "[buffs]") {
    PartyBuffs buffs;
    REQUIRE_FALSE(buffs.active(PartyBuff::ProtectionFromFire, 0));
    REQUIRE(buffs.power(PartyBuff::ProtectionFromFire, 0) == 0);
    buffs.cast(PartyBuff::ProtectionFromFire, 600, 12, 4);
    REQUIRE(buffs.active(PartyBuff::ProtectionFromFire, 599));
    REQUIRE(buffs.power(PartyBuff::ProtectionFromFire, 599) == 12);
    // At the minute it lapses it is worth nothing, and after it too.
    REQUIRE_FALSE(buffs.active(PartyBuff::ProtectionFromFire, 600));
    REQUIRE(buffs.power(PartyBuff::ProtectionFromFire, 900) == 0);
    // Casting again overwrites rather than stacking, which is all one record
    // per slot can do.
    buffs.cast(PartyBuff::ProtectionFromFire, 1200, 5, 2);
    REQUIRE(buffs.power(PartyBuff::ProtectionFromFire, 900) == 5);
    // Out-of-range slots are refused rather than written past the array.
    buffs.cast(-1, 1200, 99, 9);
    buffs.cast(static_cast<int>(kPartyBuffCount), 1200, 99, 9);
    REQUIRE_FALSE(buffs.active(-1, 0));
    REQUIRE(buffs.power(static_cast<int>(kPartyBuffCount) + 5, 0) == 0);
}

TEST_CASE("the protections answer the five resistance columns", "[buffs]") {
    PartyBuffs buffs;
    // MONSTERS.TXT's order: fire, electricity, cold, poison, magic.
    REQUIRE(kResistanceBuffs.size() == 5);
    REQUIRE(kResistanceBuffs[0] == PartyBuff::ProtectionFromFire);
    REQUIRE(kResistanceBuffs[1] == PartyBuff::ProtectionFromElectricity);
    REQUIRE(kResistanceBuffs[4] == PartyBuff::ProtectionFromMagic);
    buffs.cast(PartyBuff::ProtectionFromElectricity, 100, 9, 3);
    REQUIRE(buffs.resistance(1, 0) == 9);
    REQUIRE(buffs.resistance(0, 0) == 0);
    REQUIRE(buffs.resistance(99, 0) == 0);
}

TEST_CASE("Day of Protection fills seven slots at once", "[buffs]") {
    REQUIRE(kDayOfProtectionSlots.size() == 7);
    PartyBuffs buffs;
    for (const int slot : kDayOfProtectionSlots) {
        buffs.cast(slot, 500, 30, 10);
    }
    // Every protection, and the two the run adds beyond them.
    for (std::size_t column = 0; column < kResistanceBuffs.size(); ++column) {
        REQUIRE(buffs.resistance(column, 0) == 30);
    }
    REQUIRE(buffs.active(PartyBuff::WizardEye, 0));
    REQUIRE(buffs.active(PartyBuff::DayOfProtection, 0));
    // But not the ones it leaves alone.
    REQUIRE_FALSE(buffs.active(PartyBuff::Fly, 0));
    REQUIRE_FALSE(buffs.active(PartyBuff::TorchLight, 0));
    buffs.clear();
    REQUIRE_FALSE(buffs.active(PartyBuff::WizardEye, 0));
}

TEST_CASE("every named slot says its name", "[buffs]") {
    REQUIRE(buff_name(0) == "Protection from Fire");
    REQUIRE(buff_name(static_cast<int>(PartyBuff::GuardianAngel)) == "Guardian Angel");
    REQUIRE(buff_name(static_cast<int>(PartyBuff::TorchLight)) == "Torch Light");
    // The five slots nothing read so far fills have no name.
    REQUIRE(buff_name(9).empty());
    REQUIRE(buff_name(15).empty());
}
