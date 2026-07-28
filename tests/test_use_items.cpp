// Tests for USEITEMS.TXT: the herbs' and potions' effects, what becomes of
// the item, and the mixing matrix with its explosion grades.
//
// Hermetic: the table is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/use_items.hpp"

using namespace starhaven;
using namespace starhaven::data;

namespace {

UseItemTable table() {
    std::string body =
        "USE ITEM\tnotes\r\n"
        "\t\t\t\t\tRight Click\t160\t161\t162\r\n"
        "160\therb1\tWidoweeps\tHerb\tCure 2 Hit points\tremove Item\tno\t162\tE1\r\n"
        "161\tbottle23\tMagic Potion\tBlue Potion\tCure 10 Spell points\tChange Item to 163"
        "\tE2\tno\t162\r\n"
        "162\tbottle30\tPotion Bottle\tPotion Bottle\tno effect\t-\tno\tno\tno\r\n";
    TextTable text;
    REQUIRE(TextTable::parse_body(body, text) == TextTableError::None);
    UseItemTable out;
    REQUIRE(UseItemTable::parse(text, out) == UseItemError::None);
    return out;
}

}  // namespace

TEST_CASE("a usable item's row carries its effect and its fate", "[use_items]") {
    const auto use = table();
    REQUIRE(use.size() == 3);

    const auto* herb = use.find(160);
    REQUIRE(herb != nullptr);
    REQUIRE(herb->name == "Widoweeps");
    REQUIRE(herb->cure_hit_points == 2);
    REQUIRE(herb->cure_spell_points == 0);
    REQUIRE(herb->removed_when_used);
    REQUIRE(herb->becomes_item == 0);

    // A drunk potion becomes the empty bottle its cell names.
    const auto* potion = use.find(161);
    REQUIRE(potion->cure_spell_points == 10);
    REQUIRE_FALSE(potion->removed_when_used);
    REQUIRE(potion->becomes_item == 163);

    // The empty bottle itself does nothing and goes nowhere.
    const auto* bottle = use.find(162);
    REQUIRE(bottle->cure_hit_points == 0);
    REQUIRE_FALSE(bottle->removed_when_used);
    REQUIRE(bottle->becomes_item == 0);
}

TEST_CASE("the mixing matrix answers by pair, and some pairs explode", "[use_items]") {
    const auto use = table();
    REQUIRE(use.mix(160, 160).kind == MixKind::None);
    REQUIRE(use.mix(160, 161).kind == MixKind::Item);
    REQUIRE(use.mix(160, 161).item_id == 162);
    REQUIRE(use.mix(160, 162).kind == MixKind::Explosion);
    REQUIRE(use.mix(160, 162).explosion_grade == 1);
    REQUIRE(use.mix(161, 160).kind == MixKind::Explosion);
    REQUIRE(use.mix(161, 160).explosion_grade == 2);
    // A pair the table does not know is no mix at all.
    REQUIRE(use.mix(99, 160).kind == MixKind::None);
}

TEST_CASE("a scroll's first modifier names the spell it casts", "[use_items]") {
    // ITEMS.TXT writes "S47" on Healing Touch's scroll, and Spells.txt id 47
    // is Healing Touch.
    REQUIRE(scroll_spell_of("S47") == 47);
    REQUIRE(scroll_spell_of(" S102 ") == 102);
    REQUIRE(scroll_spell_of("3d3") == 0);
    REQUIRE(scroll_spell_of("S") == 0);
    REQUIRE(scroll_spell_of("Sword") == 0);
}

TEST_CASE("the temporary families parse in the sheet's own phrasings", "[use_items]") {
    std::string body =
        "USE ITEM\tnotes\r\n"
        "\t\t\t\t\tRight Click\t160\t161\t162\t163\r\n"
        "160\tb\tEnergy\tYellow Potion\tSet Temp 7 Stats to 10\tChange Item to 163"
        "\tno\tno\tno\tno\r\n"
        "161\tb\tProtection\tOrange Potion\tSet Temp AC to 20\tChange Item to 163"
        "\tno\tno\tno\tno\r\n"
        "162\tb\tResistance\tGreen Potion\tSet Temp Resistances to 10\tChange Item to 163"
        "\tno\tno\tno\tno\r\n"
        "163\tb\tHaste\tWhite Potion\tSet Haste to 6 Hrs\tChange Item to 163"
        "\tno\tno\tno\tno\r\n";
    TextTable text;
    REQUIRE(TextTable::parse_body(body, text) == TextTableError::None);
    UseItemTable use;
    REQUIRE(UseItemTable::parse(text, use) == UseItemError::None);

    REQUIRE(use.find(160)->temp_stats == 10);
    REQUIRE(use.find(161)->temp_armor == 20);
    REQUIRE(use.find(162)->temp_resistances == 10);
    REQUIRE(use.find(163)->buff == "Haste");
    REQUIRE(use.find(163)->buff_hours == 6);
    REQUIRE(use.find(163)->cure_hit_points == 0);
}
