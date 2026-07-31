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
    state.readied = {6, 0, 22, 0};
    state.autonotes = {115, 116};
    state.remembered.push_back({"D01.blv", 12, {3, 7}, {5, 9}, {0, 4, 11}});
    state.remembered.push_back({"Oute3.odm", 15, {}, {}, {2}});
    state.turn_based = true;
    state.hourglass_turn = 9;
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
    REQUIRE(after.readied == before.readied);
    REQUIRE(after.turn_based == before.turn_based);
    REQUIRE(after.hourglass_turn == before.hourglass_turn);
    REQUIRE(after.autonotes == before.autonotes);
    // The maps the party cleared stay cleared across a save.
    REQUIRE(after.remembered.size() == 2);
    REQUIRE(after.remembered[0].file == "D01.blv");
    REQUIRE(after.remembered[0].day == 12);
    REQUIRE(after.remembered[0].opened_chests == std::set<int>{3, 7});
    REQUIRE(after.remembered[0].open_doors == std::vector<std::uint32_t>{5, 9});
    REQUIRE(after.remembered[0].dead == std::vector<std::size_t>{0, 4, 11});
    REQUIRE(after.remembered[1].file == "Oute3.odm");
    REQUIRE(after.remembered[1].dead == std::vector<std::size_t>{2});

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

TEST_CASE("the party's name round-trips", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.reputation = -12;
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.reputation == -12);
}

TEST_CASE("the conditions' minutes round-trip with them", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.party[1].poisoned = 2;
    state.party[1].poisoned_minute = 480;
    state.party[1].affliction = "Weak";
    state.party[1].affliction_minute = 520;
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.party[1].poisoned_minute == 480);
    REQUIRE(after.party[1].affliction_minute == 520);
    // An old save's missing stamps read as minute zero, not garbage.
    SaveState old;
    REQUIRE(parse_save("starhaven-save\t1\nmap\tD01.blv\n", old));
    REQUIRE(old.party[0].poisoned_minute == 0);
}

TEST_CASE("the torch and the beacon round-trip", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.torch_until = 700;
    state.beacons.push_back({"D01.Blv", 12.5f, -3.0f, 640.0f, 900});
    state.beacons.push_back({"OutE3.Odm", 1.0f, 2.0f, 3.0f, 1200});
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.torch_until == 700);
    REQUIRE(after.beacons.size() == 2);
    REQUIRE(after.beacons[0].map == "D01.Blv");
    REQUIRE(after.beacons[0].x == 12.5f);
    REQUIRE(after.beacons[0].until == 900);
    REQUIRE(after.beacons[1].map == "OutE3.Odm");
}

TEST_CASE("the wizard eye round-trips", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.eye_until = 1200;
    state.eye_rank = 1;
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.eye_until == 1200);
    REQUIRE(after.eye_rank == 1);
}

TEST_CASE("the enchantments ride the save on gear and in packs", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.party[0].equipped[0] = 12;
    state.party[0].worn_standard[0] = 3;
    state.party[0].worn_strength[0] = 9;
    state.party[2].worn_special[4] = 16;
    PackedItem carried;
    carried.item_id = 7;
    carried.standard_bonus = 1;
    carried.standard_strength = 5;
    state.packs[1].push_back(carried);
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.party[0].worn_standard[0] == 3);
    REQUIRE(after.party[0].worn_strength[0] == 9);
    REQUIRE(after.party[2].worn_special[4] == 16);
    REQUIRE(after.packs[1][0].standard_bonus == 1);
    REQUIRE(after.packs[1][0].standard_strength == 5);
}

TEST_CASE("a wand's charges ride the save", "[save]") {
    SaveState state;
    state.map_file = "OutE3.Odm";
    state.party[0].worn_charges[0] = 37;
    PackedItem wand;
    wand.item_id = 135;
    wand.charges = 12;
    state.packs[3].push_back(wand);
    SaveState after;
    REQUIRE(parse_save(save_text(state), after));
    REQUIRE(after.party[0].worn_charges[0] == 37);
    REQUIRE(after.packs[3][0].charges == 12);
}
