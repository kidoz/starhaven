#ifndef STARHAVEN_GAME_BODY_MAGIC_HPP
#define STARHAVEN_GAME_BODY_MAGIC_HPP

// The Body school's own numbers, read out of MM6.exe's spell switch.
//
// The spell-queue processor dispatches on `word [record]` — the spell id,
// 1..102 — through a jump table at `0x429c74`, one case body per spell,
// entered with the caster's skill points in a local and the mastery (1
// normal, 2 expert, 3 master) in `esi`. Every case computes its own amount
// from those two and nothing else. Body is spells 67..77, and its eleven
// cases are read here. `observed`
//
// Where SPELLS.TXT states a number in prose the two agree exactly, which is
// the check on the reading: First Aid's 5/7/10, Cure Wounds' "five plus 2
// per point", Speed's and Power's "10 points plus 2 (expert 3) per point",
// Protection from Poison's 1/2/3 a point, and the master lines that widen
// Speed and Power to the whole party. Where the prose is silent — the buff
// durations, the cure windows above normal rank — the executable's number
// is recorded here as the only source. See docs/formats/spell-switch.md.

#include <array>
#include <cstdint>

#include "game/clock.hpp"

namespace starhaven::game {

// The eleven Body spell ids, by SPELLS.TXT's own numbering.
inline constexpr int kSpellCureWeakness = 67;
inline constexpr int kSpellFirstAid = 68;
inline constexpr int kSpellProtectionFromPoison = 69;
inline constexpr int kSpellHarm = 70;
inline constexpr int kSpellCureWounds = 71;
inline constexpr int kSpellCurePoison = 72;
inline constexpr int kSpellSpeed = 73;
inline constexpr int kSpellCureDisease = 74;
inline constexpr int kSpellPower = 75;
inline constexpr int kSpellFlyingFist = 76;
inline constexpr int kSpellPowerCure = 77;

// How long a condition may have been carried and still be curable, per point
// of the school, in game minutes. The three cure cases multiply the caster's
// points by 180, 10800 and 259200 *game seconds* — three minutes, three
// hours, three days — and subtract the result from the world clock to make a
// cutoff the condition's own timestamp is tested against. `observed` at
// 0x427d95, 0x428016 and 0x428291, and the same ladder appears outside the
// school (Mind's Cure Insanity at 0x427a05), so it belongs to every "if you
// cast this spell in time" cure rather than to Body alone.
//
// SPELLS.TXT's own words agree at normal rank ("3 minutes per point") and
// under-state the two above it, calling them "1 hour" and "1 day" where the
// executable grants three of each. The executable is followed. `observed`
inline constexpr std::array<int, 3> kCureWindowMinutes{
    3, 3 * kMinutesPerHour, 3 * kMinutesPerDay};

// What a rank's cure window is worth, in game minutes, at `points`.
// **And it is not one ladder — the earlier reading is narrowed.** Spirit's
// Remove Curse (`0x426b0e`) and Raise Dead (`0x427016`) multiply by **180,
// 3600 and 86400** seconds: three minutes, one hour, one day, exactly the
// words their rows use. Body's three cures, Mind's Cure Insanity and
// Spirit's Resurrection (`0x427282`) multiply by 180, 10800 and 259200 —
// three of each unit. So a timed cure uses one of two ladders, and which
// one is per spell rather than per school. `observed`
inline constexpr std::array<int, 3> kPlainCureWindowMinutes{3, kMinutesPerHour, kMinutesPerDay};

// The spells measured on the plain ladder: Remove Curse and Raise Dead.
[[nodiscard]] inline constexpr bool cure_uses_plain_ladder(int spell_id) noexcept {
    return spell_id == 49 || spell_id == 53;
}

[[nodiscard]] inline constexpr std::int64_t cure_window_minutes(int points, int rank,
                                                                int spell_id = 0) noexcept {
    const int held = points < 1 ? 1 : points;
    const int band = rank < 0 ? 0 : rank > 2 ? 2 : rank;
    const auto& ladder =
        cure_uses_plain_ladder(spell_id) ? kPlainCureWindowMinutes : kCureWindowMinutes;
    return static_cast<std::int64_t>(held) * ladder[static_cast<std::size_t>(band)];
}

// First Aid heals a flat 5, 7 or 10 by rank and takes no account of skill —
// the case sets the amount from the mastery alone. `observed` 0x427e2e.
inline constexpr std::array<int, 3> kFirstAidHeal{5, 7, 10};

[[nodiscard]] inline constexpr int first_aid_heal(int rank) noexcept {
    const int band = rank < 0 ? 0 : rank > 2 ? 2 : rank;
    return kFirstAidHeal[static_cast<std::size_t>(band)];
}

// Cure Wounds heals `2 × points + 5` at every rank: all three branches of
// its case compute the same thing. `observed` 0x427f82.
[[nodiscard]] inline constexpr int cure_wounds_heal(int points) noexcept {
    return 2 * points + 5;
}

// Power Cure heals `2 × points + 10` and walks all four party records —
// the case loops from 0x908f34 in strides of 0x161c, the player record's
// own size, healing each. `observed` 0x428591.
[[nodiscard]] inline constexpr int power_cure_heal(int points) noexcept {
    return 2 * points + 10;
}

// First Aid also shortens the target's recovery "by an amount equal to the
// caster's skill", which the case does by passing the raw points to the
// recovery drain — so the shortening is in the world clock's own units.
// `observed` 0x427ea9; see docs/formats/player-record.md for the unit.
[[nodiscard]] inline constexpr float first_aid_recovery_relief(int points) noexcept {
    return static_cast<float>(points) / 128.0f;
}

// Speed and Power both grant `10 + 2 × points` at normal rank and
// `10 + 3 × points` at expert and master; master widens them to the whole
// party instead of raising the number again. `observed` 0x428123, 0x4283c7.
[[nodiscard]] inline constexpr int body_stat_bonus(int points, int rank) noexcept {
    return 10 + (rank >= 1 ? 3 : 2) * points;
}

[[nodiscard]] inline constexpr bool body_buff_hits_party(int rank) noexcept { return rank >= 2; }

// Protection from Poison gives one, two or three points of poison
// resistance a point by rank. `observed` 0x427ec6.
[[nodiscard]] inline constexpr int poison_shield(int points, int rank) noexcept {
    const int band = rank < 0 ? 0 : rank > 2 ? 2 : rank;
    return points * (band + 1);
}

// How long Protection from Poison, Speed and Power last: their cases
// multiply the caster's points by 3600 game seconds — **one hour a point**,
// at every rank. SPELLS.TXT never says. `observed` 0x427ee8, 0x428143,
// 0x4283e7.
inline constexpr int kBodyBuffMinutesPerPoint = kMinutesPerHour;

[[nodiscard]] inline constexpr std::int64_t body_buff_minutes(int points) noexcept {
    return static_cast<std::int64_t>(points < 1 ? 1 : points) * kBodyBuffMinutesPerPoint;
}

// What one of the school's three healing spells gives, or zero when the
// spell is not one of them and the table's prose must answer instead.
[[nodiscard]] inline constexpr int traced_heal(int spell_id, int points, int rank) noexcept {
    switch (spell_id) {
        case kSpellFirstAid:
            return first_aid_heal(rank);
        case kSpellCureWounds:
            return cure_wounds_heal(points);
        case kSpellPowerCure:
            return power_cure_heal(points);
        default:
            return 0;
    }
}

// Power Cure's case walks every party record; the other two take one target.
[[nodiscard]] inline constexpr bool heal_reaches_party(int spell_id) noexcept {
    return spell_id == kSpellPowerCure;
}

// The condition each cure names is now in src/game/conditions.hpp, where
// the whole run is numbered — Weak 1, Poisoned 6, Diseased 7, read from
// these very cases.

// The sound each Body spell speaks with, taken from its own case: the
// school's voices run from 7000 in tens, one step a spell. `observed`
[[nodiscard]] inline constexpr int body_spell_sound(int spell_id) noexcept {
    return 7000 + (spell_id - kSpellCureWeakness) * 10;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_BODY_MAGIC_HPP
