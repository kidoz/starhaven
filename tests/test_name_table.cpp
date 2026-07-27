// Tests for npcnames.txt, the game's own list of given names.
//
// Hermetic: the fixture is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/name_table.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

NameTable names(const std::string& body) {
    TextTable table;
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    NameTable out;
    REQUIRE(NameTable::parse(table, out) == NameTableError::None);
    return out;
}

}  // namespace

TEST_CASE("the two columns are two lists", "[names]") {
    const auto table = names("Male\tFemale\r\nAaron\tAlice\r\nAbe\tAllison\r\n");
    REQUIRE(table.male().size() == 2);
    REQUIRE(table.female().size() == 2);
    REQUIRE(table.male()[0] == "Aaron");
    REQUIRE(table.female()[1] == "Allison");
}

TEST_CASE("one column running out does not end the other", "[names]") {
    // The shipped file has more of one than the other, so a blank cell is the
    // end of that column and not of the row.
    const auto table = names("Male\tFemale\r\nAaron\tAlice\r\n\tAmber\r\n\tAnne\r\n");
    REQUIRE(table.male().size() == 1);
    REQUIRE(table.female().size() == 3);
}

TEST_CASE("asking past the end wraps", "[names]") {
    const auto table = names("Male\tFemale\r\nAaron\tAlice\r\nAbe\tAllison\r\n");
    REQUIRE(table.name(false, 0) == "Aaron");
    REQUIRE(table.name(false, 2) == "Aaron");
    REQUIRE(table.name(true, 3) == "Allison");
}

TEST_CASE("an empty column answers with nothing", "[names]") {
    const auto table = names("Male\tFemale\r\nAaron\t\r\n");
    REQUIRE(table.female().empty());
    REQUIRE(table.name(true, 0).empty());
}

TEST_CASE("a table without the two headings is refused", "[names]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("a\tb\r\nAaron\tAlice\r\n", table) == TextTableError::None);
    NameTable out;
    REQUIRE(NameTable::parse(table, out) == NameTableError::NoHeader);
}
