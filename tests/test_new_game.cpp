#include "game/new_game.hpp"

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <string>
#include <string_view>

#include "game/player.hpp"  // kEyeHeight
#include "game/shop.hpp"    // kStartingGold

using namespace starhaven::game;
using starhaven::Mm6Random;

namespace {

// A dealt party without the name table: classes, faces and names set by
// hand, the numbers from the engine's own roller and deriver.
std::array<Character, 4> dealt_party() {
    std::array<Character, 4> party;
    Mm6Random random{7};
    const std::array<std::string_view, 4> classes{"Knight", "Cleric", "Sorcerer", "Archer"};
    for (std::size_t i = 0; i < party.size(); ++i) {
        Character& who = party[i];
        who.class_name = std::string(classes[i]);
        who.face = static_cast<int>(3 * i + 2) % kFaceCount;
        who.name = std::string("Member ") + std::to_string(i + 1);
        roll_attributes(who, random);
        derive_start(who);
    }
    return party;
}

// A seed the builder must leave untouched when it refuses a party.
SaveState marked_seed() {
    SaveState state;
    state.map_file = "sentinel";
    state.gold = 12345;
    return state;
}

}  // namespace

TEST_CASE("a complete party is ready and the seed carries the traced start", "[new_game]") {
    const auto party = dealt_party();
    REQUIRE(party_ready(party));

    SaveState seed = marked_seed();
    REQUIRE(make_new_game_state(party, true, seed));

    REQUIRE(seed.map_file == std::string(kNewGameMap));
    REQUIRE(seed.x == kNewGameX);
    REQUIRE(seed.y == kNewGameHeight + kEyeHeight);
    REQUIRE(seed.z == kNewGameZ);
    REQUIRE(seed.yaw == 2.0f * std::numbers::pi_v<float> * static_cast<float>(kNewGameFacing) /
                            static_cast<float>(kFacingTurn));
    REQUIRE(seed.pitch == 0.0f);
    REQUIRE(seed.minutes == 0);
    REQUIRE(seed.gold == kStartingGold);
    REQUIRE(seed.food == kStartingFood);
    REQUIRE(seed.bank_gold == 0);
    REQUIRE(seed.remembered.empty());
    REQUIRE(seed.bits.size() == 1);
    REQUIRE(seed.bits.count(81) == 1);
    REQUIRE(seed.packs[0].size() == 1);
    REQUIRE(seed.packs[0].front().item_id == 505);
    REQUIRE(seed.packs[0].front().width == 2);
    REQUIRE(seed.packs[0].front().height == 2);
    for (std::size_t i = 0; i < party.size(); ++i) {
        REQUIRE(seed.party[i].name == party[i].name);
        REQUIRE(seed.party[i].class_name == party[i].class_name);
        REQUIRE(seed.party[i].hit_points == party[i].hit_points);
        REQUIRE(seed.party[i].max_spell_points == party[i].max_spell_points);
    }
}

TEST_CASE("the opening quest's letter is the only thing the flag adds", "[new_game]") {
    SaveState plain = marked_seed();
    REQUIRE(make_new_game_state(dealt_party(), false, plain));
    REQUIRE(plain.bits.empty());
    REQUIRE(plain.packs[0].empty());
    REQUIRE(plain.gold == kStartingGold);
}

TEST_CASE("an unfinished member keeps the world closed and the seed untouched", "[new_game]") {
    const auto rejected = [](std::array<Character, 4> party) {
        SaveState seed = marked_seed();
        REQUIRE_FALSE(party_ready(party));
        REQUIRE_FALSE(make_new_game_state(party, true, seed));
        REQUIRE(seed.map_file == "sentinel");
        REQUIRE(seed.gold == 12345);
    };

    rejected([] {
        auto party = dealt_party();
        party[0].name.clear();
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[1].face = -1;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[1].face = kFaceCount;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[2].class_name = "Lich";
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[2].attributes[0] = 0;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[3].hit_points = 0;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[3].max_hit_points = 0;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[3].hit_points = party[3].max_hit_points + 1;
        return party;
    }());
    // A non-caster begins with no spell points at all; a caster, with some.
    rejected([] {
        auto party = dealt_party();
        party[0].max_spell_points = 5;
        return party;
    }());
    rejected([] {
        auto party = dealt_party();
        party[1].max_spell_points = 0;
        party[1].spell_points = 0;
        return party;
    }());
}

TEST_CASE("confirming twice rebuilds the same seed, never a second letter", "[new_game]") {
    const auto party = dealt_party();

    SaveState first;
    REQUIRE(make_new_game_state(party, true, first));
    SaveState second;
    REQUIRE(make_new_game_state(party, true, second));

    REQUIRE(first.bits.size() == second.bits.size());
    REQUIRE(first.packs[0].size() == second.packs[0].size());
    REQUIRE(first.gold == second.gold);
    REQUIRE(first.packs[0].front().item_id == second.packs[0].front().item_id);
}

TEST_CASE("the seed round-trips through the save format it is shaped as", "[new_game]") {
    const auto party = dealt_party();
    SaveState seed;
    REQUIRE(make_new_game_state(party, true, seed));

    SaveState parsed;
    REQUIRE(parse_save(save_text(seed), parsed));
    REQUIRE(parsed.map_file == std::string(kNewGameMap));
    REQUIRE(parsed.gold == kStartingGold);
    REQUIRE(parsed.food == kStartingFood);
    REQUIRE(parsed.bits.count(81) == 1);
    REQUIRE(parsed.packs[0].size() == 1);
    REQUIRE(parsed.packs[0].front().item_id == 505);
    REQUIRE(parsed.party[0].name == party[0].name);
    REQUIRE(parsed.party[3].max_hit_points == party[3].max_hit_points);
}
