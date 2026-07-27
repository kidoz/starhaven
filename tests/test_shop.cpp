// Tests for trading: the merchant's words, what a shop stocks, and prices.
//
// Hermetic: the tables are synthesized from the formats described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/shop.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::MerchantTextTable merchant() {
    std::string body = "\tBuy\tSell\tRepair\tIdentify\r\n";
    body += "Not enough gold\tYou don't have enough money.\tn/a\tToo poor.\tToo poor.\r\n";
    body +=
        "no merchant skill\tAn excellent choice!\tI'll give you this.\tFixed.\tThat is a %24.\r\n";
    body += "regular merchant skill\tOrdinarily I sell for %25.\tHard bargain.\tFixed.\tA %24.\r\n";
    body += "good merchant skill\tI try to sell for %25.\tWorth %26.\tFixed.\tA %24.\r\n";
    body += "wrong type of merchant\tn/a\tI am a %28.\tNo idea how.\tNo idea what.\r\n";
    body += "Unnecessary\tn/a\tBeyond my knowledge.\tThat isn't broken!\tAlready known.\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MerchantTextTable out;
    REQUIRE(data::MerchantTextTable::parse(table, out) == data::MerchantTextError::None);
    return out;
}

data::ItemStatsEntry item(int value) {
    data::ItemStatsEntry e;
    e.name = "Longsword";
    e.value = value;
    return e;
}

}  // namespace

TEST_CASE("the merchant has a line for each situation and action", "[shop]") {
    const auto words = merchant();
    REQUIRE(words.line(data::MerchantSituation::NoSkill, data::MerchantAction::Buy) ==
            "An excellent choice!");
    REQUIRE(words.line(data::MerchantSituation::NotEnoughGold, data::MerchantAction::Buy) ==
            "You don't have enough money.");
    REQUIRE(words.line(data::MerchantSituation::Unnecessary, data::MerchantAction::Repair) ==
            "That isn't broken!");
}

TEST_CASE("n/a is a situation that cannot arise, not a line", "[shop]") {
    const auto words = merchant();
    REQUIRE(words.line(data::MerchantSituation::NotEnoughGold, data::MerchantAction::Sell).empty());
    REQUIRE(words.line(data::MerchantSituation::WrongType, data::MerchantAction::Buy).empty());
    // Three of the 24 cells say n/a; the shipped table has 21 filled too.
    REQUIRE(words.filled() == 21);
}

TEST_CASE("a table without the action headings is refused", "[shop]") {
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body("a\tb\tc\r\n", table) == data::TextTableError::None);
    data::MerchantTextTable out;
    REQUIRE(data::MerchantTextTable::parse(table, out) == data::MerchantTextError::NoHeader);
}

TEST_CASE("a stock line gives a level and a kind", "[shop]") {
    REQUIRE(parse_stock("L1 Weap").level == 1);
    REQUIRE(parse_stock("L1 Weap").type == data::ItemGenerationType::Weapon);
    REQUIRE(parse_stock("L2 Sword,Dagger").level == 2);
    REQUIRE(parse_stock("L2 Sword,Dagger").type == data::ItemGenerationType::Weapon);
    REQUIRE(parse_stock("L2 Bows").type == data::ItemGenerationType::Missile);
    REQUIRE(parse_stock("L3 Plate").type == data::ItemGenerationType::Armor);
    REQUIRE(parse_stock("L1 Potion").type == data::ItemGenerationType::Potion);
}

TEST_CASE("a stock line that says nothing useful still says a level", "[shop]") {
    // Better than refusing: the shop still stocks something.
    REQUIRE(parse_stock("").level == 1);
    REQUIRE(parse_stock("").type == data::ItemGenerationType::Any);
    REQUIRE(parse_stock("All Weap").level == 1);
    REQUIRE(parse_stock("All Weap").type == data::ItemGenerationType::Weapon);
}

TEST_CASE("a shop charges its own multiplier and pays less", "[shop]") {
    // The table gives 1.5 and 2 for the multiplier; what a shop pays is this
    // engine's.
    REQUIRE(asking_price(item(100), 1.5f) == 150);
    REQUIRE(asking_price(item(100), 2.0f) == 200);
    REQUIRE(offer_price(item(100)) == 50);
    // Nothing is ever free, however cheap.
    REQUIRE(asking_price(item(0), 2.0f) == 1);
    REQUIRE(offer_price(item(1)) == 1);
}

TEST_CASE("a shop with no tables behind it has bare shelves", "[shop]") {
    // The generator needs four tables; a missing install must not crash a
    // counter, only empty it.
    data::BuildingStatsEntry shop;
    shop.id = 1;
    shop.stock_a = "L1 Weap";
    REQUIRE(stock_of(shop, {}, {}, {}, {}, 1).empty());
}
