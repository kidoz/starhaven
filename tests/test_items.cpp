#include <catch2/catch_test_macros.hpp>

#include "core/data/item_stats.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

TEST_CASE("a weapon's dice are decoded at load", "[items]") {
    TextTable table;
    REQUIRE(
        TextTable::parse_body("Item #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\n"
                              "0\tblank\tPlaceholder\t0\tother\tMisc\t0\t0\n"
                              "1\tsynthetic\tTest blade\t10\tweapon\tSword\t3d7+2\t0\n",
                              table) == TextTableError::None);
    ItemStatsTable items;
    REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::None);
    REQUIRE(items.entries().size() == 2);
    REQUIRE(items.at(0)->modifier_1_dice.empty());
    const auto& dice = items.at(1)->modifier_1_dice;
    REQUIRE(dice.count == 3);
    REQUIRE(dice.sides == 7);
    REQUIRE(dice.bonus == 2);
    REQUIRE(dice.lowest() == 5);
    REQUIRE(dice.highest() == 23);
}
