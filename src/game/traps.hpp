#ifndef STARHAVEN_GAME_TRAPS_HPP
#define STARHAVEN_GAME_TRAPS_HPP

// Chests that bite back, the way the original bites.
//
// The whole mechanism is traced from `MM6.exe` (see
// docs/formats/event-tables.md, "The chest flags word"): each chest record
// carries its own trapped bit, and opening a trapped chest rolls the acting
// character's Disarm Traps — level times 2/3/4 by rank, doubled by an
// equipped Pendragon, Hades or item "of Thievery", plus the Tinker,
// Locksmith and Burglar hirelings' promised points — plus a d10 against
// five times the map's "Lock 0-10" column. Falling short spawns one of four
// elemental trap objects at the chest, and when its animation ends it
// detonates: everyone within 1,024 units takes one shared roll of **five
// plus the map's "Trap 0-10" column of d20s** — the column MapStats' own
// header annotates "D20's" — reduced by their resistance, unless their
// Perception lets them leap clear. All of that is `observed`.

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

// Which resistance answers each element, in the type names the tables
// write and combat.hpp's `resistance_to` resolves. The executable's own
// damage-type numbers are fire 2, electric 3, cold 4, poison 5. `observed`
[[nodiscard]] constexpr std::string_view trap_element_type(TrapElement element) noexcept {
    switch (element) {
    case TrapElement::Fire:
        return "Fire";
    case TrapElement::Cold:
        return "Cold";
    case TrapElement::Electric:
        return "Elec";
    default:
        return "Pois";
    }
}

// The blast, rolled once and shared by the whole party: five plus the
// map's trap number of d20s. MapStats' own header annotates the column
// "D20's", and the executable rolls exactly that. The detonation only
// reaches a party within 1,024 units of the chest — always true at
// interaction range, so the shell need not test it. `observed`
[[nodiscard]] inline int trap_damage(int trap_dice, Mm6Random& random) {
    int damage = 5;
    for (int d = 0; d < trap_dice; ++d) {
        damage += 1 + static_cast<int>(random.next() % 20);
    }
    return damage;
}

// The original stores a skill as one byte: level in the low six bits,
// expert as bit 6, master as bit 7 — and the dodge below reads that byte
// raw, mastery bits and all, so an expert leaps far better than their
// points alone say. The bit layout is the original's; the thresholds
// behind `rank_of` remain this engine's.
[[nodiscard]] inline int packed_skill_byte(int points) noexcept {
    const int rank = rank_of(points);
    return points | (rank >= 2 ? 0x80 : rank == 1 ? 0x40 : 0);
}

// A member's leap clear of the blast, by Perception: the executable rolls
// rand % (P + 20) against 20, so one point never dodges and the chance
// climbs as (P − 1)/(P + 20). No skill, no roll. The dodger speaks line 33
// — that is the original's own touch, not this engine's. `observed`
[[nodiscard]] inline bool perception_dodges(int packed_perception, Mm6Random& random) {
    if (packed_perception <= 0) {
        return false;
    }
    const auto bound = static_cast<std::uint32_t>(packed_perception) + 20U;
    return static_cast<int>(random.next() % bound) > 20;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAPS_HPP
