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
