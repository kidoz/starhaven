// Tests for what worn enchantments grant, from the bonus tables' own words.
#include <catch2/catch_test_macros.hpp>

#include "game/enchant.hpp"

using namespace starhaven;
using namespace starhaven::game;

TEST_CASE("a standard bonus grants its named stat at its rolled strength", "[enchant]") {
    data::StandardBonusEntry might;
    might.stat = "Might";
    might.name_suffix = "of Might";
    const auto power = standard_power(might, 7);
    REQUIRE(power.attributes[static_cast<std::size_t>(Attribute::Might)] == 7);
    REQUIRE(power.any());

    data::StandardBonusEntry fire;
    fire.stat = "Fire Resistance";
    REQUIRE(standard_power(fire, 12)
                .resistances[static_cast<std::size_t>(data::Resistance::Fire)] == 12);

    data::StandardBonusEntry armor;
    armor.stat = "Armor Class";
    REQUIRE(standard_power(armor, 5).armor_class == 5);

    data::StandardBonusEntry hp;
    hp.stat = "Hit Points";
    REQUIRE(standard_power(hp, 10).hit_points == 10);
}

TEST_CASE("a special bonus parses the phrasings it ships", "[enchant]") {
    data::SpecialBonusEntry protection;
    protection.effect = "+10 to all Resistances.";
    const auto shield = special_power(protection);
    REQUIRE(shield.resistances[static_cast<std::size_t>(data::Resistance::Fire)] == 10);
    REQUIRE(shield.resistances[static_cast<std::size_t>(data::Resistance::Magic)] == 10);

    data::SpecialBonusEntry frost;
    frost.effect = "Adds 6-8 points of Cold damage.";
    const auto rider = special_power(frost);
    REQUIRE(rider.extra_damage.count == 1);
    REQUIRE(rider.extra_damage.sides == 3);
    REQUIRE(rider.extra_damage.bonus == 5);
    REQUIRE(rider.damage_element == "Cold");

    data::SpecialBonusEntry eclipse;
    eclipse.effect = "+10 Spell points and Regenerate Spell points over time.";
    REQUIRE(special_power(eclipse).spell_points == 10);

    // Prose beyond the phrasings grants nothing, honestly.
    data::SpecialBonusEntry vampiric;
    vampiric.effect = "Drain Hit Points from target.";
    REQUIRE_FALSE(special_power(vampiric).any());
}

TEST_CASE("the enchanted name reads the affix's own shape", "[enchant]") {
    data::StandardBonusEntry might;
    might.name_suffix = "of Might";
    REQUIRE(enchanted_name("Longsword", &might, nullptr) == "Longsword of Might");

    data::SpecialBonusEntry frost;
    frost.name_affix = "of Frost";
    REQUIRE(enchanted_name("Axe", nullptr, &frost) == "Axe of Frost");
    data::SpecialBonusEntry vampiric;
    vampiric.name_affix = "Vampiric";
    REQUIRE(enchanted_name("Dagger", nullptr, &vampiric) == "Vampiric Dagger");
    REQUIRE(enchanted_name("Club", nullptr, nullptr) == "Club");
}
