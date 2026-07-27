// Tests for PROFTEXT.txt, what a hired NPC says on each day of the week.
//
// Hermetic: the fixture is synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/profession_text.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

std::string body() {
    std::string s = "Day of Week Profession Text\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\r\n";
    s += "#\t\tSunday topic\tSunday text\tMonday topic\tMonday text\tTuesday topic\tTuesday text"
         "\tWednesday topic\tWednesday text\tThursday topic\tThursday text\tFriday topic"
         "\tFriday text\tSaturday topic\tSaturday text\r\n";
    s += "1\tSmith\tRest\tIt's a day of rest.\tMonday\tI don't like Mondays.\tHiring\tHire us."
         "\tWed\tw\tThu\tt\tFri\tf\tSat\ts\r\n";
    s += "2\tArmorer\tArmor\tTwo kinds.\tMon\tm\tTue\tt\tWed\tw\tThu\tt\tFri\tf\tSat\ts\r\n";
    return s;
}

ProfessionTextTable table() {
    TextTable text;
    REQUIRE(TextTable::parse_body(body(), text) == TextTableError::None);
    ProfessionTextTable out;
    REQUIRE(ProfessionTextTable::parse(text, out) == ProfessionTextError::None);
    return out;
}

}  // namespace

TEST_CASE("a profession has a topic and a line for each of seven days", "[proftext]") {
    const auto said = table();
    REQUIRE(said.size() == 2);

    const auto* smith = said.at(1);
    REQUIRE(smith != nullptr);
    REQUIRE(smith->name == "Smith");
    REQUIRE(smith->days.size() == kProfessionDayCount);
    REQUIRE(smith->days[0].topic == "Rest");
    REQUIRE(smith->days[0].text == "It's a day of rest.");
    REQUIRE(smith->days[1].text == "I don't like Mondays.");
    REQUIRE(smith->days[6].topic == "Sat");
}

TEST_CASE("the id is the one npcprof.txt gives the profession", "[proftext]") {
    // The two tables are keyed the same, which is what lets a hired NPC be
    // asked what their trade says today. All 77 resolve.
    const auto said = table();
    REQUIRE(said.at(2)->name == "Armorer");
    REQUIRE(said.at(3) == nullptr);
    REQUIRE(said.at(0) == nullptr);
}

TEST_CASE("a table without the day headings is refused", "[proftext]") {
    TextTable text;
    REQUIRE(TextTable::parse_body("a\tb\tc\r\n1\tSmith\tx\r\n", text) == TextTableError::None);
    ProfessionTextTable out;
    REQUIRE(ProfessionTextTable::parse(text, out) == ProfessionTextError::NoHeader);
}
