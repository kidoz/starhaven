// Tests for what a person in an establishment has to say.
//
// Hermetic: the tables and the person are built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/conversation.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

data::InterfaceStrings words() {
    // Only the ids the substitution needs, at their shipped positions.
    std::string body;
    for (int id = 0; id <= kStringEvening; ++id) {
        const char* text = id == kStringMorning   ? "morning"
                           : id == kStringDay     ? "day"
                           : id == kStringEvening ? "evening"
                           : id == kStringSir     ? "sir"
                           : id == kStringLady    ? "lady"
                                                  : "x";
        body += std::to_string(id) + "\t" + text + "\r\n";
    }
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::InterfaceStrings out;
    REQUIRE(data::InterfaceStrings::parse(table, out) == data::InterfaceStringsError::None);
    return out;
}

data::NpcPersonalityTable personalities() {
    std::string body = "Msg#\tNotes\tPeasant BTB\tThief BT\r\n";
    body += "Beg\t\t1\t0\r\n";
    body += "Bribe\t\t1\t1\r\n";
    body += "Threat\t\t1\t1\r\n";
    body += "1\tRep ok, 1st greet\tGood day!\tName's Sam.\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::NpcPersonalityTable out;
    REQUIRE(data::NpcPersonalityTable::parse(table, out) == data::NpcStatsError::None);
    return out;
}

data::NpcDialogueTable dialogue() {
    std::string topics =
        "Text Number\t\tNotes\r\n#\tTopic\t\r\n1\tThe Letter\t\r\n2\tA Rumour\t\r\n";
    std::string texts = "Text Number\t\t\r\n#\tText\tNotes\r\n1\tHere is your money.\t\r\n";
    data::TextTable a;
    data::TextTable b;
    REQUIRE(data::TextTable::parse_body(topics, a) == data::TextTableError::None);
    REQUIRE(data::TextTable::parse_body(texts, b) == data::TextTableError::None);
    data::NpcDialogueTable out;
    REQUIRE(data::NpcDialogueTable::parse(a, b, out) == data::NpcStatsError::None);
    return out;
}

data::ProfessionTextTable trade() {
    std::string body = "Day of Week Profession Text\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\r\n";
    body += "#\t\tSunday topic\tSunday text\tMonday topic\tMonday text\tTuesday topic\tTuesday text"
            "\tWed\tw\tThu\tt\tFri\tf\tSat\ts\r\n";
    body += "4\tScholar\tPotions\tCombining potions.\tMon\tm\tTue\tt\tWed\tw\tThu\tt\tFri\tf"
            "\tSat\ts\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::ProfessionTextTable out;
    REQUIRE(data::ProfessionTextTable::parse(table, out) == data::ProfessionTextError::None);
    return out;
}

world::SessionNpc scholar() {
    world::SessionNpc p;
    p.name = "Wilma";
    p.profession_id = 4;
    p.profession = "Scholar";
    p.personality = "Peasant";
    p.topics = {1, 2, 0};
    return p;
}

}  // namespace

TEST_CASE("a person greets you as their personality does", "[talk]") {
    const auto talk = talk_to(scholar(), dialogue(), personalities(), trade(), GameClock{});
    REQUIRE(talk.who == "Wilma, Scholar");
    REQUIRE(talk.greeting == "Good day!");
    // A Peasant entertains all three approaches; a Thief only two.
    REQUIRE(talk.approaches.size() == 3);
}

TEST_CASE("a topic with no words is still a topic", "[talk]") {
    // 22 of the 493 shipped topics have a label and nothing to say; dropping
    // them would renumber what an NPC can be asked.
    const auto talk = talk_to(scholar(), dialogue(), personalities(), trade(), GameClock{});
    REQUIRE(talk.topics.size() == 2);
    REQUIRE(talk.topics[0] == "The Letter");
    REQUIRE(talk.topics[1] == "A Rumour");
    REQUIRE(topic_answer(scholar(), dialogue(), 0) == "Here is your money.");
    REQUIRE(topic_answer(scholar(), dialogue(), 1).empty());
    REQUIRE(topic_answer(scholar(), dialogue(), 9).empty());
}

TEST_CASE("what a trade talks about follows the day", "[talk]") {
    // PROFTEXT gives a topic per weekday; day one is a Sunday.
    REQUIRE(talk_to(scholar(), dialogue(), personalities(), trade(), GameClock{}).today ==
            "Potions");
    GameClock monday;
    monday.advance_hours(kHoursPerDay);
    REQUIRE(talk_to(scholar(), dialogue(), personalities(), trade(), monday).today == "Mon");
}

TEST_CASE("a person the tables know nothing about still has a name", "[talk]") {
    world::SessionNpc stranger;
    stranger.name = "Nobody";
    const auto talk = talk_to(stranger, dialogue(), personalities(), trade(), GameClock{});
    REQUIRE(talk.who == "Nobody");
    REQUIRE(talk.greeting.empty());
    REQUIRE(talk.today.empty());
    REQUIRE(talk.topics.empty());
    REQUIRE(talk.approaches.empty());
}

TEST_CASE("the placeholders are filled from the interface strings", "[talk]") {
    const auto w = words();
    const Speech morning{"Wilma", "Aaron", false, 9};
    REQUIRE(substitute("Good %05!  I'm %01.", morning, w) == "Good morning!  I'm Wilma.");

    const Speech evening{"Wilma", "Aaron", false, 20};
    REQUIRE(substitute("Good %05!", evening, w) == "Good evening!");
    const Speech noon{"Wilma", "Aaron", false, 13};
    REQUIRE(substitute("Good %05!", noon, w) == "Good day!");
}

TEST_CASE("the honorific follows who is being spoken to", "[talk]") {
    const auto w = words();
    REQUIRE(substitute("Peace, %06.", Speech{"W", "Aaron", false, 12}, w) == "Peace, sir.");
    REQUIRE(substitute("Peace, %06.", Speech{"W", "Alice", true, 12}, w) == "Peace, lady.");
    REQUIRE(substitute("%06 %02.", Speech{"W", "Aaron", false, 12}, w) == "sir Aaron.");
}

TEST_CASE("a code nobody has read stays visible", "[talk]") {
    // Blanking it would silently eat text; 12 of the codes are still unread.
    const auto w = words();
    REQUIRE(substitute("takes %17 percent", Speech{}, w) == "takes %17 percent");
    REQUIRE(substitute("100%", Speech{}, w) == "100%");
    REQUIRE(substitute("%0", Speech{}, w) == "%0");
}
