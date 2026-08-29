#include "game/new_game.hpp"

#include <numbers>
#include <ranges>

#include "game/inventory.hpp"
#include "game/player.hpp"  // kEyeHeight, the camera's height over the feet
#include "game/shop.hpp"    // kStartingGold

namespace starhaven::game {
namespace {

[[nodiscard]] bool member_ready(const Character& who) noexcept {
    if (who.name.empty() || who.face < 0 || who.face >= kFaceCount) {
        return false;
    }
    if (std::ranges::find(kBaseClasses, who.class_name) == kBaseClasses.end()) {
        return false;
    }
    for (std::size_t a = 0; a < kAttributeCount; ++a) {
        if (who.attributes[a] < 1) {
            return false;
        }
    }
    if (who.max_hit_points < 1 || who.hit_points < 1 || who.hit_points > who.max_hit_points) {
        return false;
    }
    if (who.spell_points < 0 || who.spell_points > who.max_spell_points) {
        return false;
    }
    // A caster begins with points to spend; a non-caster has none at all.
    if (casts_spells(who.class_name) != (who.max_spell_points >= 1)) {
        return false;
    }
    return true;
}

}  // namespace

bool party_ready(const std::array<Character, 4>& party) noexcept {
    return std::ranges::all_of(party, [](const Character& who) { return member_ready(who); });
}

bool make_new_game_state(const std::array<Character, 4>& party, bool seed_opening_quest,
                         SaveState& out) {
    if (!party_ready(party)) {
        return false;
    }
    SaveState fresh;
    fresh.map_file = std::string(kNewGameMap);
    // The traced start (see src/game/party.hpp): the game's x and y are this
    // engine's x and z, its z is the height, and the facing is 512 of 2048
    // to the turn. A save carries the camera, so the eyes ride over the feet.
    fresh.x = kNewGameX;
    fresh.y = kNewGameHeight + kEyeHeight;
    fresh.z = kNewGameZ;
    fresh.yaw = 2.0f * std::numbers::pi_v<float> * static_cast<float>(kNewGameFacing) /
                static_cast<float>(kFacingTurn);
    fresh.pitch = 0.0f;
    fresh.minutes = 0;
    fresh.gold = kStartingGold;
    fresh.food = kStartingFood;
    if (seed_opening_quest) {
        // Bit 81's designers' note reads "Set when the party starts", and its
        // journal line is "Show Sulman's letter to Andover Potbello" — so the
        // opening party holds The Letter (item 505) with the bit lit.
        fresh.bits.insert(81);
        Pack pack;
        (void)pack.add(505, 2, 2);  // The Letter, at its scroll art's size
        fresh.packs[0] = pack.items();
    }
    fresh.party = party;
    out = std::move(fresh);
    return true;
}

}  // namespace starhaven::game
