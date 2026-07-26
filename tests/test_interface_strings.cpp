// Tests for Global.txt, the interface's own vocabulary.
//
// Hermetic: the fixture is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/interface_strings.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

InterfaceStrings strings(const std::string& body) {
    TextTable table;
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    InterfaceStrings out;
    REQUIRE(InterfaceStrings::parse(table, out) == InterfaceStringsError::None);
    return out;
}

}  // namespace

TEST_CASE("strings resolve by id", "[strings]") {
    const auto words = strings("0\tAC\r\n1\tAccuracy\r\n2\tAdd to Stat\r\n");
    REQUIRE(words.size() == 3);
    REQUIRE(words.at(0) == "AC");
    REQUIRE(words.at(2) == "Add to Stat");
}

TEST_CASE("the first string has id zero, not one", "[strings]") {
    // Reading a blank cell as zero would make the id of "AC" ambiguous with
    // every unnumbered row, so a missing number has to read as negative.
    const auto words = strings("\theading\r\n0\tAC\r\n");
    REQUIRE(words.size() == 1);
    REQUIRE(words.at(0) == "AC");
}

TEST_CASE("an id with no string, or none at all, is not a string", "[strings]") {
    const auto words = strings("0\tAC\r\n5\tSpeed\r\n");
    REQUIRE(words.size() == 6);
    REQUIRE(words.at(3).empty());
    REQUIRE(words.at(-1).empty());
    REQUIRE(words.at(999).empty());
}

TEST_CASE("a table with no numbered rows is refused", "[strings]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("name\tvalue\r\n", table) == TextTableError::None);
    InterfaceStrings out;
    REQUIRE(InterfaceStrings::parse(table, out) == InterfaceStringsError::NoRows);
}
