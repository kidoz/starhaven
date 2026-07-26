// Tests for the bit-keyed journal tables.
//
// Hermetic: the fixtures are synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/journal.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

// Quests.txt: "Q Bit | Actual Quest Note Text | Notes | Old Quest Note Text".
std::string quest_body() {
    std::string s;
    s += "Q Bit\tActual Quest Note Text\tNotes\tOld Quest Note Text\r\n";
    s += "1\t\t 1 D09, key to open D05\t D09, key to open D05\r\n";
    s += "81\tShow the letter to Andover.\tSet when the party starts\tShow the letter\r\n";
    return s;
}

// Autonote.txt: "Note bit | Autonote Text | Category".
std::string autonote_body() {
    std::string s;
    s += "Note bit\tAutonote Text\tCategory\r\n";
    s += "1\t5 hit points cured by the fountain.\tStat\r\n";
    s += "2\tA teacher lives here.\tTeacher\r\n";
    return s;
}

JournalTable quests() {
    TextTable table;
    REQUIRE(TextTable::parse_body(quest_body(), table) == TextTableError::None);
    JournalTable out;
    REQUIRE(JournalTable::parse(table, 1, JournalTable::kNoColumn, 2, 3, out) ==
            JournalError::None);
    return out;
}

}  // namespace

TEST_CASE("journal lines are keyed by their bit", "[journal]") {
    const auto table = quests();
    REQUIRE(table.size() == 2);
    REQUIRE(table.at(81)->text == "Show the letter to Andover.");
    REQUIRE(table.at(1) != nullptr);
    REQUIRE(table.at(999) == nullptr);
}

TEST_CASE("a bit with no text is kept", "[journal]") {
    // 460 of the 512 shipped quest bits have no player-facing text. Dropping
    // them would renumber every line after, and the numbering is what the game
    // sets.
    const auto table = quests();
    const auto* unwritten = table.at(1);
    REQUIRE(unwritten != nullptr);
    REQUIRE_FALSE(unwritten->has_text());
    REQUIRE(unwritten->notes == "1 D09, key to open D05");
    REQUIRE(table.written() == 1);
}

TEST_CASE("a column the file does not have stays empty", "[journal]") {
    // Quests have no category; autonotes have no designers' notes column.
    const auto table = quests();
    REQUIRE(table.at(81)->category.empty());
    REQUIRE(table.at(81)->alternate == "Show the letter");

    TextTable notes;
    REQUIRE(TextTable::parse_body(autonote_body(), notes) == TextTableError::None);
    JournalTable autonotes;
    REQUIRE(JournalTable::parse(notes, 1, 2, JournalTable::kNoColumn, JournalTable::kNoColumn,
                                autonotes) == JournalError::None);
    REQUIRE(autonotes.at(1)->category == "Stat");
    REQUIRE(autonotes.at(1)->notes.empty());
    REQUIRE(autonotes.at(1)->alternate.empty());
    REQUIRE(autonotes.written() == 2);
}

TEST_CASE("the heading row is not a journal line", "[journal]") {
    // Its first cell is a label, not a number, so it falls out on its own.
    const auto table = quests();
    for (const auto& e : table.entries()) {
        REQUIRE(e.bit > 0);
    }
}

TEST_CASE("a table with no numbered rows is refused", "[journal]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("Heading\tText\r\nnot a bit\tsomething\r\n", table) ==
            TextTableError::None);
    JournalTable out;
    REQUIRE(JournalTable::parse(table, 1, JournalTable::kNoColumn, JournalTable::kNoColumn,
                                JournalTable::kNoColumn, out) == JournalError::NoRows);
}
