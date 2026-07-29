// Tests for what a character carries and for picking things up.
//
// Hermetic: the item table and the session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/inventory.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

// ITEMS.TXT: "Item # | Pic File | Name | Value | ...".
data::ItemStatsTable items() {
    std::string body =
        "\r\nItem #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
        "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    // Ids are the direct index the binaries use, so row 0 is real and comes
    // first; the shipped file has it blank.
    body += "0\t\t\t0\t\t\t0\t0\t0\t0\t\t0\t0\t0\t0\t\r\n";
    body += "1\tlsword1\tLongsword\t50\tWeapon\tSword\t3d3\t0\t8\t1\tLongsword\t1\t4\t0\t0\t\r\n";
    body += "2\tring1\tRing\t100\tRing\t\t0\t0\t8\t1\tRing\t2\t4\t0\t0\t\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::ItemStatsTable out;
    REQUIRE(data::ItemStatsTable::parse(table, out) == data::ItemStatsError::None);
    return out;
}

world::MapSession with_object(int item_id, render::Vec3 at) {
    world::MapSession s;
    s.kind = world::MapKind::Outdoor;
    s.objects.push_back({0, "Longsword", item_id, at});
    return s;
}

}  // namespace

TEST_CASE("an icon claims another cell only once it overhangs", "[inventory]") {
    // The 229 item icons are 9 to 140 pixels wide and 12 to 289 tall. Rounding
    // up outright leaves one that cannot be carried at all.
    REQUIRE(cells_across(9) == 1);
    REQUIRE(cells_across(32) == 1);
    REQUIRE(cells_across(45) == 1);
    REQUIRE(cells_across(47) == 2);
    REQUIRE(cells_across(140) == 4);
    REQUIRE(cells_across(289) == 9);
    REQUIRE(cells_across(0) == 0);
}

TEST_CASE("a pack fills from the top left", "[inventory]") {
    Pack pack;
    REQUIRE(pack.add(1, 2, 2));
    REQUIRE(pack.add(2, 1, 1));
    REQUIRE(pack.items()[0].x == 0);
    REQUIRE(pack.items()[0].y == 0);
    REQUIRE(pack.items()[1].x == 2);
    REQUIRE(pack.items()[1].y == 0);
}

TEST_CASE("a cell an item covers is occupied, not just its corner", "[inventory]") {
    Pack pack;
    REQUIRE(pack.add(1, 2, 3));
    REQUIRE(pack.at(0, 0) != nullptr);
    REQUIRE(pack.at(1, 2) != nullptr);
    REQUIRE(pack.at(1, 2)->item_id == 1);
    REQUIRE(pack.at(2, 0) == nullptr);
    REQUIRE(pack.at(0, 3) == nullptr);
}

TEST_CASE("a pack with no room refuses", "[inventory]") {
    Pack pack;
    for (int i = 0; i < kPackWidth * kPackHeight; ++i) {
        REQUIRE(pack.add(i + 1, 1, 1));
    }
    REQUIRE_FALSE(pack.add(999, 1, 1));
    REQUIRE(pack.size() == static_cast<std::size_t>(kPackWidth * kPackHeight));
}

TEST_CASE("an item bigger than the pack goes nowhere", "[inventory]") {
    Pack pack;
    REQUIRE_FALSE(pack.add(1, kPackWidth + 1, 1));
    REQUIRE_FALSE(pack.add(1, 1, kPackHeight + 1));
    REQUIRE_FALSE(pack.add(0, 1, 1));
    REQUIRE(pack.empty());
}

TEST_CASE("the party takes what it is standing over", "[inventory]") {
    auto session = with_object(1, {0, 0, 0});
    assets::AssetCache cache;  // no archives: every icon is missing
    std::array<Pack, 4> packs;

    const std::string taken = take_nearby(session, items(), cache, {0, 400, 0}, packs);
    REQUIRE(taken == "Longsword");
    REQUIRE(session.objects.empty());
    REQUIRE(packs[0].size() == 1);
    REQUIRE(packs[0].items()[0].item_id == 1);
}

TEST_CASE("an item whose art is missing still takes a cell", "[inventory]") {
    // Otherwise a missing picture silently makes an item impossible to carry.
    auto session = with_object(1, {0, 0, 0});
    assets::AssetCache cache;
    std::array<Pack, 4> packs;
    (void)take_nearby(session, items(), cache, {0, 0, 0}, packs);
    REQUIRE(packs[0].items()[0].width == 1);
    REQUIRE(packs[0].items()[0].height == 1);
}

TEST_CASE("something out of reach stays where it is", "[inventory]") {
    auto session = with_object(1, {2000, 0, 0});
    assets::AssetCache cache;
    std::array<Pack, 4> packs;
    REQUIRE(take_nearby(session, items(), cache, {0, 0, 0}, packs).empty());
    REQUIRE(session.objects.size() == 1);
}

TEST_CASE("the eye being above the ground does not put loot out of reach", "[inventory]") {
    // The camera is the party's eyes. Measuring in three dimensions makes it
    // unable to pick up the thing it is standing on.
    auto session = with_object(1, {0, 0, 0});
    assets::AssetCache cache;
    std::array<Pack, 4> packs;
    REQUIRE(take_nearby(session, items(), cache, {0, kPickUpRange + 100.0f, 0}, packs) ==
            "Longsword");
}

TEST_CASE("a party with no room leaves it lying there", "[inventory]") {
    auto session = with_object(1, {0, 0, 0});
    assets::AssetCache cache;
    std::array<Pack, 4> packs;
    for (auto& pack : packs) {
        for (int i = 0; i < kPackWidth * kPackHeight; ++i) {
            REQUIRE(pack.add(i + 1, 1, 1));
        }
    }
    REQUIRE(take_nearby(session, items(), cache, {0, 0, 0}, packs).empty());
    REQUIRE(session.objects.size() == 1);
}

TEST_CASE("an object that is not an item is not loot", "[inventory]") {
    auto session = with_object(0, {0, 0, 0});
    assets::AssetCache cache;
    std::array<Pack, 4> packs;
    REQUIRE(take_nearby(session, items(), cache, {0, 0, 0}, packs).empty());
    REQUIRE(session.objects.size() == 1);
}

TEST_CASE("an unknown thing stays unknown until named", "[inventory]") {
    starhaven::game::Pack pack;
    REQUIRE(pack.add(7, 1, 1, false));
    REQUIRE(pack.add(8, 1, 1));
    REQUIRE_FALSE(pack.items()[0].identified);
    REQUIRE(pack.items()[1].identified);
    // Naming reveals exactly the unknown one; asking again finds nothing.
    REQUIRE(pack.identify_at(pack.items()[0].x, pack.items()[0].y));
    REQUIRE(pack.items()[0].identified);
    REQUIRE_FALSE(pack.identify_at(pack.items()[0].x, pack.items()[0].y));
}
