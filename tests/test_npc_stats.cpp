// Tests for the NPC and profession tables.
//
// Hermetic: the fixtures are synthesized from the format described in
// docs/formats/text-tables.md.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/data/npc_stats.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

std::string npc_body() {
    std::string s;
    s += "NPC Data (Special)\tUnter\t\tBTB check\t\t\tCurrent\tProfession\tJoin\tNews\tEvent"
         "\tEvent\tEvent\t\r\n";
    s += "#\tName\tPic\tState\tFame\tRep\t2D Location\t 1 - 76\tY / N\tY / N\t# A\t# B\t# C"
         "\tNotes\r\n";
    s += "1\tAndover Potbello\t81\t0\t0\t0\t92\t74\t0\t0\t1\t296\t0\tgives money\r\n";
    s += "2\tMaria\t126\t0\t0\t0\t92\t48\t0\t1\t8\t0\t0\tgives a clue\r\n";
    s += "3\tWandering Sam\t187\t0\t0\t0\t-1\t0\t1\t0\t0\t0\t0\troams\r\n";
    return s;
}

std::string profession_body() {
    std::string s;
    s += "\t\t\t\t\t\t\t\r\n";
    s += "\tNPC\tRandom\tJoin\t\t\t\t\r\n";
    s += "#\tProfessions\tChance\tCost/w\tPersonality\tAction Text\tIn Party Benefit\tJoin Text"
         "\r\n";
    s += "\t\t\t\t\t\t\t\r\n";
    s += "48\tGypsy\t10\t100\tMerchant\t\tTells fortunes.\tI read palms.\r\n";
    s += "74\tFollower of Baa\t5\t0\tCleric\t\tPrays loudly.\tBaa sent me.\r\n";
    return s;
}

NpcTable npcs() {
    TextTable table;
    REQUIRE(TextTable::parse_body(npc_body(), table) == TextTableError::None);
    NpcTable out;
    REQUIRE(NpcTable::parse(table, out) == NpcStatsError::None);
    return out;
}

NpcProfessionTable professions() {
    TextTable table;
    REQUIRE(TextTable::parse_body(profession_body(), table) == TextTableError::None);
    NpcProfessionTable out;
    REQUIRE(NpcProfessionTable::parse(table, out) == NpcStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("people parse into typed rows", "[npc]") {
    const auto people = npcs();
    REQUIRE(people.size() == 3);

    const auto& first = people.entries()[0];
    REQUIRE(first.id == 1);
    REQUIRE(first.name == "Andover Potbello");
    REQUIRE(first.picture == 81);
    REQUIRE(first.building_id == 92);
    REQUIRE(first.profession_id == 74);
    REQUIRE(first.events[0] == 1);
    REQUIRE(first.events[1] == 296);
    REQUIRE(first.notes == "gives money");
}

TEST_CASE("the join and news columns are numbers, not letters", "[npc]") {
    // The heading reads "Y / N" but the cells hold 1 and 0.
    const auto people = npcs();
    REQUIRE_FALSE(people.entries()[0].has_news);
    REQUIRE(people.entries()[1].has_news);
    REQUIRE(people.entries()[2].can_join);
}

TEST_CASE("a location of -1 means the person is in no building", "[npc]") {
    // Eighteen shipped rows say -1. Treating it as an id makes eighteen people
    // permanently fail to resolve; treating it as zero would hide that the
    // table said something specific.
    const auto people = npcs();
    const auto& roamer = people.entries()[2];
    REQUIRE(roamer.building_id == -1);
    REQUIRE_FALSE(roamer.placed());
    REQUIRE(people.in_building(-1).empty());
}

TEST_CASE("everyone in one establishment is found", "[npc]") {
    const auto people = npcs();
    const auto inside = people.in_building(92);
    REQUIRE(inside.size() == 2);
    REQUIRE(inside[0]->name == "Andover Potbello");
    REQUIRE(inside[1]->name == "Maria");
    REQUIRE(people.in_building(999).empty());
}

TEST_CASE("professions parse and resolve by id", "[npc]") {
    const auto jobs = professions();
    REQUIRE(jobs.size() == 2);
    REQUIRE(jobs.at(48)->name == "Gypsy");
    REQUIRE(jobs.at(48)->hire_cost == 100);
    REQUIRE(jobs.at(48)->personality == "Merchant");
    REQUIRE(jobs.at(74)->name == "Follower of Baa");
    REQUIRE(jobs.at(1) == nullptr);
    REQUIRE(jobs.at(0) == nullptr);
}

TEST_CASE("tables without their headers are refused", "[npc]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("a\tb\r\n1\t2\r\n", table) == TextTableError::None);

    NpcTable people;
    REQUIRE(NpcTable::parse(table, people) == NpcStatsError::NoHeader);
    NpcProfessionTable jobs;
    REQUIRE(NpcProfessionTable::parse(table, jobs) == NpcStatsError::NoHeader);
}

namespace {

std::string topic_body() {
    std::string s;
    s += "Text Number From NPC Events.Doc\t\tNotes\r\n";
    s += "#\tTopic\t\r\n";
    s += "1\tThe Letter\t\r\n";
    s += "2\tThe Seal\t\r\n";
    s += "3\tA Rumour\t\r\n";
    return s;
}

std::string text_body() {
    std::string s;
    s += "Text Number From NPC Events.Doc\t\t\r\n";
    s += "#\tText\tNotes\r\n";
    s += "1\tHere is your money.\t\r\n";
    s += "2\tThe seal is broken.\t\r\n";
    return s;
}

NpcDialogueTable dialogue() {
    TextTable topics;
    TextTable texts;
    REQUIRE(TextTable::parse_body(topic_body(), topics) == TextTableError::None);
    REQUIRE(TextTable::parse_body(text_body(), texts) == TextTableError::None);
    NpcDialogueTable out;
    REQUIRE(NpcDialogueTable::parse(topics, texts, out) == NpcStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("topics and texts merge on their shared numbering", "[npc]") {
    const auto said = dialogue();
    REQUIRE(said.size() == 3);
    REQUIRE(said.at(1)->topic == "The Letter");
    REQUIRE(said.at(1)->text == "Here is your money.");
    REQUIRE(said.at(2)->topic == "The Seal");
}

TEST_CASE("a topic with no words is kept, not dropped", "[npc]") {
    // Nineteen of the shipped topics have a label and nothing to say. Dropping
    // them would break the numbering the NPC event columns rely on.
    const auto said = dialogue();
    REQUIRE(said.at(3) != nullptr);
    REQUIRE(said.at(3)->topic == "A Rumour");
    REQUIRE(said.at(3)->text.empty());
    REQUIRE(said.at(99) == nullptr);
}

TEST_CASE("news keeps its map column as written", "[npc]") {
    // The column is headed "Map" but its values do not line up with the map
    // list, so it is not resolved to one.
    std::string body = "Regional News\t\t\t\r\n"
                       "#\tMap\tTopic\tNews Text\r\n"
                       "1\t40\tGoblinwatch\tThe keep is full of goblins.\r\n"
                       "2\t1\tSweet Water\tThe town was beautiful once.\r\n";
    TextTable table;
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    NpcNewsTable news;
    REQUIRE(NpcNewsTable::parse(table, news) == NpcStatsError::None);

    REQUIRE(news.size() == 2);
    REQUIRE(news.entries()[0].map_value == 40);
    REQUIRE(news.entries()[0].topic == "Goblinwatch");
    REQUIRE(news.entries()[1].map_value == 1);
}

TEST_CASE("dialogue tables without their headers are refused", "[npc]") {
    TextTable empty;
    REQUIRE(TextTable::parse_body("a\tb\r\n1\t2\r\n", empty) == TextTableError::None);

    NpcDialogueTable said;
    REQUIRE(NpcDialogueTable::parse(empty, empty, said) == NpcStatsError::NoHeader);
    NpcNewsTable news;
    REQUIRE(NpcNewsTable::parse(empty, news) == NpcStatsError::NoHeader);
}

namespace {

// npcbtb.txt: "Msg# | Notes | <personality> <approach initials>...".
std::string personality_body() {
    std::string s;
    s += "Msg#\tNotes\tPeasant BTB\tThief BT\tEvil Fanatic T\r\n";
    s += "Beg\t\t1\t0\t0\r\n";
    s += "Bribe\t\t1\t1\t0\r\n";
    s += "Threat\t\t1\t1\t1\r\n";
    s += "1\tRep ok, 1st greet\tGood day!\tName's Sam.\tBaa is coming.\r\n";
    s += "2\tI accept your beg\tOh, all right.\tn/a\tn/a\r\n";
    s += "3\tI don't like begging\tn/a\tI gave at the Guild.\tBeg elsewhere.\r\n";
    return s;
}

NpcPersonalityTable personalities() {
    TextTable table;
    REQUIRE(TextTable::parse_body(personality_body(), table) == TextTableError::None);
    NpcPersonalityTable out;
    REQUIRE(NpcPersonalityTable::parse(table, out) == NpcStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("the header names the personalities, minus its approach reminder", "[npc]") {
    // The columns read "Peasant BTB", "Thief BT" — the suffix repeats what the
    // three rows below state exactly, so it is not part of the name.
    const auto matrix = personalities();
    REQUIRE(matrix.size() == 3);
    REQUIRE(matrix.entries()[0].name == "Peasant");
    REQUIRE(matrix.entries()[1].name == "Thief");
    REQUIRE(matrix.entries()[2].name == "Evil Fanatic");
}

TEST_CASE("the first three rows say which approaches work", "[npc]") {
    const auto matrix = personalities();
    const auto& peasant = *matrix.find("Peasant");
    REQUIRE(peasant.allows_approach(NpcApproach::Beg));
    REQUIRE(peasant.allows_approach(NpcApproach::Bribe));
    REQUIRE(peasant.allows_approach(NpcApproach::Threat));

    const auto& thief = *matrix.find("Thief");
    REQUIRE_FALSE(thief.allows_approach(NpcApproach::Beg));
    REQUIRE(thief.allows_approach(NpcApproach::Bribe));
}

TEST_CASE("the profession table's Fanatic is this file's Evil Fanatic", "[npc]") {
    // The two files are the only place either name appears, and every other
    // personality matches outright; without the suffix match one profession of
    // the twelve would resolve to nothing.
    const auto matrix = personalities();
    REQUIRE(matrix.find("Fanatic") != nullptr);
    REQUIRE(matrix.find("Fanatic")->name == "Evil Fanatic");
    REQUIRE(matrix.find("Merchant") == nullptr);
}

TEST_CASE("messages are keyed by their own number and share one note", "[npc]") {
    const auto matrix = personalities();
    REQUIRE(matrix.note(1) == "Rep ok, 1st greet");
    REQUIRE(matrix.find("Peasant")->message(1) == "Good day!");
    REQUIRE(matrix.find("Thief")->message(1) == "Name's Sam.");
    REQUIRE(matrix.find("Peasant")->message(0).empty());
    REQUIRE(matrix.find("Peasant")->message(99).empty());
}

TEST_CASE("n/a means the personality never says that line", "[npc]") {
    // It appears exactly where the matrix makes the line impossible: a Peasant
    // accepts begging so never refuses it, and a Thief refuses so never
    // accepts. All 39 shipped pairs agree.
    const auto matrix = personalities();
    REQUIRE(matrix.find("Peasant")->message(2) == "Oh, all right.");
    REQUIRE(matrix.find("Peasant")->message(3).empty());
    REQUIRE(matrix.find("Thief")->message(2).empty());
    REQUIRE(matrix.find("Thief")->message(3) == "I gave at the Guild.");
}

TEST_CASE("a table without the Msg# header is refused", "[npc]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("a\tb\r\n1\t2\r\n", table) == TextTableError::None);
    NpcPersonalityTable matrix;
    REQUIRE(NpcPersonalityTable::parse(table, matrix) == NpcStatsError::NoHeader);
}

TEST_CASE("no name is not a personality", "[npc]") {
    // The suffix match that lets "Fanatic" find "Evil Fanatic" would otherwise
    // let an empty name find the first entry, and give a person with no
    // profession somebody else's greeting.
    const auto matrix = personalities();
    REQUIRE(matrix.find("") == nullptr);
}
