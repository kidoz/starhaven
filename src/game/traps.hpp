#ifndef STARHAVEN_GAME_TRAPS_HPP
#define STARHAVEN_GAME_TRAPS_HPP

// Chests that bite back, the way the original bites.
//
// The mechanism is traced from `MM6.exe`'s chest-open path (see
// docs/formats/event-tables.md, "The chest flags word"): each chest record
// carries its own trapped bit, and opening a trapped chest rolls the acting
// character's Disarm Traps — level times 2/3/4 by rank, doubled by an
// equipped Pendragon, Hades or item "of Thievery", plus the Tinker,
// Locksmith and Burglar hirelings' promised points — plus a d10 against
// five times the map's "Lock 0-10" column. Falling short detonates one of
// four elemental traps at the chest. All of that is `observed`. What the
// blast rolls for damage is not traced, so the dice below are this
// engine's own and say so.

#include <array>
#include <cstdint>
#include <string_view>

#include "core/random.hpp"
#include "game/party.hpp"
#include "game/skills.hpp"

namespace starhaven::game {

// The three items the original's Disarm fetch checks by hand: two artifacts
// by `ITEMS.TXT` id, and any equipped piece bearing `SPCITEMS.TXT` row 35 —
// "of Thievery", whose prose says only "Increases chance of Disarming" and
// turns out to mean a doubling. `observed`
inline constexpr int kPendragonId = 410;
inline constexpr int kHadesId = 415;
inline constexpr int kOfThieverySpecial = 35;

// The disarm value the check spends: (level, doubled per item, plus the
// hirelings' points) times 2, 3 or 4 for normal, expert, master — the
// hireling points multiply too, in the original's own order. `observed`
// The rank thresholds behind `rank_of` remain this engine's (see
// skills.hpp); the original keeps them with its teachers.
[[nodiscard]] constexpr int disarm_value(int points, int rank, int item_doublings,
                                         int hireling_points) noexcept {
    int value = points;
    for (int i = 0; i < item_doublings; ++i) {
        value += value;
    }
    value += hireling_points;
    return value * (rank >= 2 ? 4 : rank == 1 ? 3 : 2);
}

// How many doublings a character's gear grants: Pendragon on the back,
// Hades in the hand, anything "of Thievery" anywhere — each once, broken
// pieces mute. `observed`
[[nodiscard]] inline int disarm_item_doublings(const Character& who) noexcept {
    int doublings = 0;
    const auto worn_working = [&](Slot slot) {
        const auto i = static_cast<std::size_t>(slot);
        return !who.equipped_broken[i] ? who.equipped[i] : 0;
    };
    if (worn_working(Slot::Cloak) == kPendragonId) {
        ++doublings;
    }
    if (worn_working(Slot::Weapon) == kHadesId) {
        ++doublings;
    }
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (who.equipped[i] != 0 && !who.equipped_broken[i] &&
            who.worn_special[i] == kOfThieverySpecial) {
            ++doublings;
            break;
        }
    }
    return doublings;
}

// The character's whole value against a chest, from their sheet.
[[nodiscard]] inline int character_disarm_value(const Character& who, int hireling_points) {
    int points = 0;
    if (const auto it = who.skills.find("Disarm Traps"); it != who.skills.end()) {
        points = it->second;
    }
    if (points <= 0) {
        return 0;
    }
    return disarm_value(points, rank_of(points), disarm_item_doublings(who), hireling_points);
}

// The roll: value plus a d10 must beat five times the map's lock number.
// A value of zero never rolls — the original fails it outright. `observed`
[[nodiscard]] inline bool disarm_check(int value, int lock_difficulty, Mm6Random& random) {
    if (value <= 0) {
        return false;
    }
    return value + static_cast<int>(random.next() % 10) > 5 * lock_difficulty;
}

// The four traps a chest can spring, picked uniformly — `DOBJLIST.BIN`'s
// firetrap 811, coldtrap 812, electrap 813 and poistrap 814. `observed`
enum class TrapElement : std::uint8_t { Fire, Cold, Electric, Poison };

[[nodiscard]] inline TrapElement trap_element(Mm6Random& random) {
    return static_cast<TrapElement>(random.next() % 4);
}

[[nodiscard]] constexpr std::string_view trap_element_name(TrapElement element) noexcept {
    switch (element) {
    case TrapElement::Fire:
        return "fire";
    case TrapElement::Cold:
        return "cold";
    case TrapElement::Electric:
        return "electric";
    default:
        return "poison";
    }
}

// The blast, on everyone standing. The original hands the harm to the
// spawned trap object's own detonation, which is not yet traced, so the
// amount here — the lock number's worth of d6 each — is this engine's own
// reading. `inferred`
struct TrapBlast {
    std::array<int, 4> damage{};  // per member; zero where down
};

[[nodiscard]] inline TrapBlast spring_trap(int difficulty, const std::array<Character, 4>& party,
                                           Mm6Random& random) {
    TrapBlast out;
    for (std::size_t i = 0; i < party.size(); ++i) {
        if (party[i].hit_points <= 0) {
            continue;
        }
        int rolled = 0;
        for (int d = 0; d < difficulty; ++d) {
            rolled += 1 + static_cast<int>(random.next() % 6);
        }
        out.damage[i] = rolled;
    }
    return out;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAPS_HPP
