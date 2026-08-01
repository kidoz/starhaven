
TEST_CASE("a weapon's dice are decoded once, at load", "[items]") {
    // The original's runtime row keeps Mod1 as bytes at +0x16, decoded when
    // the table is read; this engine used to re-parse the text on every roll.
    data::TextTable table;
    data::ItemStatsTable items;
    REQUIRE(data::ItemStatsTable::parse(table, items) == data::ItemStatsError::None);
    for (const auto& row : items.entries()) {
        // Whatever the text says, the decoded field agrees with it.
        REQUIRE(row.modifier_1_dice.count == data::parse_dice(row.modifier_1).count);
        REQUIRE(row.modifier_1_dice.sides == data::parse_dice(row.modifier_1).sides);
    }
}
