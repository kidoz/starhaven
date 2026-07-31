#ifndef STARHAVEN_GAME_FIRE_DARK_HPP
#define STARHAVEN_GAME_FIRE_DARK_HPP

// The Fire and Dark schools' own numbers, read out of MM6.exe's spell switch.
//
// The method is the one that opened Body: each case in the table at
// `0x429c74` is entered with the caster's skill points in a local and the
// mastery in `esi` as 1, 2 or 3, and computes its amount from those alone.
// Fire is spells 1..11 and Dark 89..99; the ids that fly a projectile share
// the launcher bodies and carry no number of their own, so what is here is
// the seven Fire cases and six Dark cases that do. `observed`
//
// `SPELLS.TXT` states a figure for eleven of the thirteen and the executable
// agrees with every one: Fire Blast's 3/5/7 shots, Meteor Shower's 8/12/16
// meteors, Shrapmetal's 3/5/7 fragments, Reanimate's 10/20/30 hit points a
// point, Mass Curse's 2/3/4 minutes a point, Day of Protection's 2/3/4×
// skill, Armageddon's one, two and three casts a day, Torch Light's and
// Protection from Fire's hour a point. Where it is silent — Torch Light's
// brightness, Ring of Fire's two radii, Haste's true base — the number here
// is the executable's only. See docs/formats/spell-switch.md.

#include <array>
#include <cstddef>
#include <cstdint>

#include "game/clock.hpp"

namespace starhaven::game {

// Fire, spells 1..11.
inline constexpr int kSpellTorchLight = 1;
inline constexpr int kSpellProtectionFromFire = 3;
inline constexpr int kSpellHaste = 5;
inline constexpr int kSpellRingOfFire = 7;
inline constexpr int kSpellFireBlast = 8;
inline constexpr int kSpellMeteorShower = 9;

// Dark, spells 89..99.
inline constexpr int kSpellReanimate = 89;
inline constexpr int kSpellMassCurse = 91;
inline constexpr int kSpellShrapmetal = 92;
inline constexpr int kSpellDayOfProtection = 94;
inline constexpr int kSpellArmageddon = 98;

[[nodiscard]] inline constexpr int rank_band(int rank) noexcept {
    return rank < 0 ? 0 : rank > 2 ? 2 : rank;
}

// How bright Torch Light burns: the case sets 2, 3 or 4 by rank beside its
// duration. The table says only "brighter" and "brightest". `observed`
// 0x423068.
inline constexpr std::array<int, 3> kTorchBrightness{2, 3, 4};

// Ring of Fire's radius, measured: **512 at normal, 1024 at expert and
// master**, in the world's own units. The table says only "small radius"
// and "larger radius". `observed` 0x423999.
inline constexpr std::array<int, 3> kRingOfFireRadius{512, 1024, 1024};

// How many things the two volley spells throw. `observed` 0x423b5a and
// 0x423d44, and the table says the same in words.
inline constexpr std::array<int, 3> kFireBlastShots{3, 5, 7};
inline constexpr std::array<int, 3> kMeteorShowerMeteors{8, 12, 16};
inline constexpr std::array<int, 3> kShrapmetalFragments{3, 5, 7};

// Haste's duration, in world seconds: `(points + 64) × 60` at normal and
// expert, `3840 + 180 × points` at master. So the base is **sixty-four
// minutes**, not the hour the table rounds it to, and the step is a minute
// a point at the lower two ranks and three at the top — which is what the
// prose says apart from the four extra minutes. `observed` 0x42389b and
// 0x4237b6.
inline constexpr int kHasteBaseMinutes = 64;

[[nodiscard]] inline constexpr std::int64_t haste_minutes(int points, int rank) noexcept {
    const int step = rank_band(rank) >= 2 ? 3 : 1;
    return kHasteBaseMinutes + static_cast<std::int64_t>(step) * points;
}

// Mass Curse holds for two, three or four minutes a point. `observed`
// 0x429296.
inline constexpr std::array<int, 3> kMassCurseMinutesPerPoint{2, 3, 4};

// Reanimate gives a creature ten, twenty or thirty hit points a point.
// `observed` 0x42910f.
inline constexpr std::array<int, 3> kReanimatePerPoint{10, 20, 30};

// Day of Protection casts everything at two, three or four times the
// caster's Dark Magic. `observed` 0x42960d.
inline constexpr std::array<int, 3> kDayOfProtectionMultiple{2, 3, 4};

// Armageddon may be cast once, twice or three times a day, counted against
// the byte at `+0x1619` of the caster. `observed` 0x429a09.
inline constexpr std::array<int, 3> kArmageddonPerDay{1, 2, 3};

// Torch Light and Protection from Fire both last an hour a point, the same
// figure the Body school's buffs keep. `observed` 0x423074 and 0x423710.
inline constexpr int kFireBuffMinutesPerPoint = kMinutesPerHour;

// Protection from Fire gives one, two or three points of fire resistance a
// point, exactly as its Body counterpart does for poison. `observed`
// 0x4236ee.
[[nodiscard]] inline constexpr int fire_shield(int points, int rank) noexcept {
    return points * (rank_band(rank) + 1);
}

[[nodiscard]] inline constexpr int by_rank(const std::array<int, 3>& ladder, int rank) noexcept {
    return ladder[static_cast<std::size_t>(rank_band(rank))];
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_FIRE_DARK_HPP
