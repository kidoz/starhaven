// Tests for the party's spell-buff array, on the shape MM6.exe's own
// clearing routine gives it.
#include <catch2/catch_test_macros.hpp>

#include "game/buffs.hpp"
#include "game/spirit_mind_light.hpp"

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

TEST_CASE("a character keeps sixteen slots of its own", "[buffs]") {
    REQUIRE(kCharacterBuffCount == 16);
    REQUIRE(kCharacterBuffBase == 0x1268);
    CharacterBuffs buffs;
    REQUIRE(buffs.power(CharacterBuff::Haste, 0) == 0);
    buffs.cast(CharacterBuff::Haste, 300, 7);
    REQUIRE(buffs.power(CharacterBuff::Haste, 299) == 7);
    REQUIRE(buffs.power(CharacterBuff::Haste, 300) == 0);
    // Meditation and Power each take two slots, and the pairs are the
    // attributes their rows name.
    buffs.cast(CharacterBuff::MeditationIntellect, 300, 16);
    buffs.cast(CharacterBuff::MeditationPersonality, 300, 16);
    REQUIRE(buffs.power(CharacterBuff::MeditationIntellect, 0) == 16);
    REQUIRE(buffs.power(CharacterBuff::MeditationPersonality, 0) == 16);
    REQUIRE(buffs.power(CharacterBuff::PowerMight, 0) == 0);
    // The slot numbers are the offsets the cases use, sixteen bytes apart.
    REQUIRE(kCharacterBuffBase + 16 * static_cast<int>(CharacterBuff::Haste) == 0x1288);
    REQUIRE(kCharacterBuffBase + 16 * static_cast<int>(CharacterBuff::MeditationIntellect) ==
            0x12c8);
    REQUIRE(kCharacterBuffBase + 16 * static_cast<int>(CharacterBuff::PowerEndurance) == 0x1318);
    buffs.clear();
    REQUIRE(buffs.power(CharacterBuff::MeditationIntellect, 0) == 0);
    // Out-of-range slots are refused.
    buffs.cast(kCharacterBuffCount + 3, 900, 5);
    REQUIRE(buffs.power(kCharacterBuffCount + 3, 0) == 0);
}

TEST_CASE("every character spell knows its slot", "[buffs]") {
    REQUIRE(character_slot_of_spell(46) == static_cast<int>(CharacterBuff::Bless));
    REQUIRE(character_slot_of_spell(51) == static_cast<int>(CharacterBuff::Heroism));
    REQUIRE(character_slot_of_spell(5) == static_cast<int>(CharacterBuff::Haste));
    REQUIRE(character_slot_of_spell(17) == static_cast<int>(CharacterBuff::Shield));
    REQUIRE(character_slot_of_spell(38) == static_cast<int>(CharacterBuff::StoneSkin));
    REQUIRE(character_slot_of_spell(48) == static_cast<int>(CharacterBuff::LuckyDay));
    REQUIRE(character_slot_of_spell(59) == static_cast<int>(CharacterBuff::Precision));
    REQUIRE(character_slot_of_spell(73) == static_cast<int>(CharacterBuff::Speed));
    // The two that take a pair are not on the single-slot list.
    REQUIRE(character_slot_of_spell(56) == -1);
    REQUIRE(character_slot_of_spell(75) == -1);
    REQUIRE(kMeditationSlots[0] == 6);
    REQUIRE(kPowerSlots[1] == 11);
    // And a damage spell owns nothing here either.
    REQUIRE(character_slot_of_spell(2) == -1);
}

TEST_CASE("the attribute slots answer for the stat their spell names", "[buffs]") {
    // Slots 4..11 are the eight the stat getter reads, in its own order.
    REQUIRE(kAttributeBuffStat.size() == 8);
    REQUIRE(buff_slot_for_stat(6) == static_cast<int>(CharacterBuff::LuckyDay));      // Luck
    REQUIRE(buff_slot_for_stat(1) == static_cast<int>(CharacterBuff::MeditationIntellect));
    REQUIRE(buff_slot_for_stat(2) == static_cast<int>(CharacterBuff::MeditationPersonality));
    REQUIRE(buff_slot_for_stat(4) == static_cast<int>(CharacterBuff::Precision));     // Accuracy
    REQUIRE(buff_slot_for_stat(5) == static_cast<int>(CharacterBuff::Speed));
    REQUIRE(buff_slot_for_stat(0) == static_cast<int>(CharacterBuff::PowerMight));
    REQUIRE(buff_slot_for_stat(3) == static_cast<int>(CharacterBuff::PowerEndurance));
    // The slots below four carry no attribute.
    REQUIRE(buff_slot_for_stat(99) == -1);
    for (int slot = 0; slot < 4; ++slot) {
        REQUIRE(buff_slot_for_stat(slot) != slot);
    }
}

TEST_CASE("the four remaining buffs carry the shape their rows give", "[buffs]") {
    // Bless, Heroism and Stone Skin all say "5 + 1 per point of skill" — to
    // hit, to damage, to armour class. The five is the row's.
    REQUIRE(kBlessBase == 5);
    CharacterBuffs buffs;
    buffs.cast(CharacterBuff::Bless, 500, 9);
    buffs.cast(CharacterBuff::Heroism, 500, 4);
    buffs.cast(CharacterBuff::StoneSkin, 500, 0);
    REQUIRE(kBlessBase + buffs.power(CharacterBuff::Bless, 0) == 14);
    REQUIRE(kBlessBase + buffs.power(CharacterBuff::Heroism, 0) == 9);
    // A slot at no power gives nothing at all, not the bare five: the caller
    // only adds the base when the slot is up.
    REQUIRE(buffs.power(CharacterBuff::StoneSkin, 0) == 0);
    // Shield carries no number; its row only halves, so the slot is a flag.
    REQUIRE(buffs.power(CharacterBuff::Shield, 0) == 0);
    buffs.cast(CharacterBuff::Shield, 500, 1);
    REQUIRE(buffs.power(CharacterBuff::Shield, 0) > 0);
    // And all four lapse with the clock like the rest.
    REQUIRE(buffs.power(CharacterBuff::Bless, 500) == 0);
}
