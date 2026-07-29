// Tests for saving the game and getting it back.
//
// Hermetic: the state is built by hand and round-tripped through the text.
#include <catch2/catch_test_macros.hpp>

#include "game/save.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

SaveState full_state() {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.x = 12.5f;
    state.y = -3.0f;
    state.z = 960.0f;
    state.yaw = 0.6f;
    state.pitch = -0.25f;
    state.minutes = 123456789;
    state.gold = 1200;
    state.bank_gold = 5000;
    state.bits = {82, 300};
    state.variables = {{25, 10}, {105, 1}};
    state.npc_topics = {{{1, 0}, 2}};
    state.npc_places = {{43, 486}};
    state.opened_chests = {0, 3};
    state.open_doors = {55, 57};
    Character& who = state.party[0];
    who.name = "Jym-Bob";
    who.class_name = "Knight";
    who.face = 3;
    who.level = 2;
    who.experience = 1500;
    who.hit_points = 17;
    who.max_hit_points = 30;
    who.attributes = {15, 12, 11, 14, 13, 12, 9};
    who.resistances = {0, 0, 5, 0, 0};
    who.equipped[0] = 1;
    who.temp_attributes[0] = 10;
    who.temp_armor = 20;
    who.haste_until = 360;
    who.known_spells = {4, 68};
    who.poisoned = 2;
    who.diseased = 1;
    who.affliction = "Asleep";
    who.equipped_broken[0] = true;
    state.packs[0] = {{505, 2, 3, 1, 2}, {66, 0, 0, 2, 2}};
    return state;
}

}  // namespace

TEST_CASE("a save round-trips whole", "[save]") {
    const SaveState before = full_state();
    SaveState after;
    REQUIRE(parse_save(save_text(before), after));

    REQUIRE(after.map_file == before.map_file);
    REQUIRE(after.x == before.x);
    REQUIRE(after.pitch == before.pitch);
    REQUIRE(after.minutes == before.minutes);
    REQUIRE(after.gold == before.gold);
    REQUIRE(after.bank_gold == 5000);
    REQUIRE(after.bits == before.bits);
    REQUIRE(after.variables == before.variables);
    REQUIRE(after.npc_topics == before.npc_topics);
    REQUIRE(after.npc_places == before.npc_places);
    REQUIRE(after.opened_chests == before.opened_chests);
    REQUIRE(after.open_doors == before.open_doors);

    const Character& who = after.party[0];
    // The name keeps its own hyphen and the class survives beside it.
    REQUIRE(who.name == "Jym-Bob");
    REQUIRE(who.class_name == "Knight");
    REQUIRE(who.level == 2);
    REQUIRE(who.experience == 1500);
    REQUIRE(who.hit_points == 17);
    REQUIRE(who.attributes == before.party[0].attributes);
    REQUIRE(who.resistances == before.party[0].resistances);
    REQUIRE(who.equipped == before.party[0].equipped);
    REQUIRE(who.temp_attributes[0] == 10);
    REQUIRE(who.temp_armor == 20);
    REQUIRE(who.haste_until == 360);
    REQUIRE(who.known_spells == std::set<int>{4, 68});
    REQUIRE(who.poisoned == 2);
    REQUIRE(who.diseased == 1);
    REQUIRE(who.affliction == "Asleep");
    REQUIRE(who.equipped_broken[0]);
    REQUIRE_FALSE(who.equipped_broken[1]);

    REQUIRE(after.packs[0].size() == 2);
    REQUIRE(after.packs[0][0].item_id == 505);
    REQUIRE(after.packs[0][0].x == 2);
    REQUIRE(after.packs[0][0].height == 2);
}

TEST_CASE("the wrong magic or version refuses", "[save]") {
    SaveState state;
    REQUIRE_FALSE(parse_save("", state));
    REQUIRE_FALSE(parse_save("something-else\t1\nmap\tOutE3.Odm\n", state));
    REQUIRE_FALSE(parse_save("starhaven-save\t99\nmap\tOutE3.Odm\n", state));
    // A save without a map has nowhere to put the party.
    REQUIRE_FALSE(parse_save("starhaven-save\t1\ngold\t5\n", state));
}

TEST_CASE("unknown record kinds are skipped, not fatal", "[save]") {
    SaveState state;
    REQUIRE(parse_save("starhaven-save\t1\nmap\tD01.blv\nfuture-thing\t7\t8\n", state));
    REQUIRE(state.map_file == "D01.blv");
}

TEST_CASE("the hired help and their wage day round-trip", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.food = 6;
    state.hired.push_back({12, 10, "Sharry"});
    state.hired.push_back({44, 33, "Cooky Tom"});
    state.wage_day = 21;

    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.food == 6);
    REQUIRE(after.hired.size() == 2);
    REQUIRE(after.hired[0].npc_id == 12);
    REQUIRE(after.hired[0].profession_id == 10);
    REQUIRE(after.hired[0].name == "Sharry");
    REQUIRE(after.hired[1].name == "Cooky Tom");
    REQUIRE(after.wage_day == 21);
    // A save from before the followers reads as none of them.
    SaveState old;
    REQUIRE(parse_save("starhaven-save\t1\nmap\tD01.blv\ngold\t50\n", old));
    REQUIRE(old.hired.empty());
    REQUIRE(old.food == 0);
}

TEST_CASE("the honors round-trip", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.awards = {53, 2};
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.awards == std::vector<int>{53, 2});
}

TEST_CASE("an unidentified item stays unidentified through a save", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    PackedItem unknown;
    unknown.item_id = 7;
    unknown.identified = false;
    state.packs[0].push_back(unknown);
    PackedItem known;
    known.item_id = 8;
    known.x = 2;
    state.packs[0].push_back(known);
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE_FALSE(after.packs[0][0].identified);
    REQUIRE(after.packs[0][1].identified);
}

TEST_CASE("the visited towns and the flight round-trip", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.visited_towns = {"OutE3.Odm", "OutD3.Odm"};
    state.fly_until = 5000;
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.visited_towns == std::vector<std::string>{"OutE3.Odm", "OutD3.Odm"});
    REQUIRE(after.fly_until == 5000);
}
