#ifndef STARHAVEN_GAME_SPIRIT_MIND_LIGHT_HPP
#define STARHAVEN_GAME_SPIRIT_MIND_LIGHT_HPP

// The Spirit, Mind and Light schools' own numbers, out of MM6.exe's spell
// switch — the same method that opened Body, Fire and Dark.
//
// Every case is entered with the caster's skill points and the mastery as 1,
// 2 or 3, and computes its amount from those alone. The ids that fly a
// projectile share the launcher bodies and carry no number; what is here is
// the cases that do. `observed`
//
// `SPELLS.TXT` states a figure for most of them and the executable agrees
// with every one. Where it is silent — the buff durations, the ten that Day
// of the Gods adds on top of its multiple, the four minutes by which the
// hour-long buffs run over — the number here is the executable's only.

#include <array>
#include <cstddef>
#include <cstdint>

#include "game/clock.hpp"

namespace starhaven::game {

// Spirit.
inline constexpr int kSpellBless = 46;
inline constexpr int kSpellHealingTouch = 47;
inline constexpr int kSpellLuckyDay = 48;
inline constexpr int kSpellRemoveCurse = 49;
inline constexpr int kSpellGuardianAngel = 50;
inline constexpr int kSpellHeroism = 51;
inline constexpr int kSpellRaiseDead = 53;
inline constexpr int kSpellSharedLife = 54;
inline constexpr int kSpellResurrection = 55;

// Mind.
inline constexpr int kSpellMeditation = 56;
inline constexpr int kSpellRemoveFear = 57;
inline constexpr int kSpellPrecision = 59;
inline constexpr int kSpellCureParalysis = 60;
inline constexpr int kSpellCureInsanity = 64;
inline constexpr int kSpellTelekinesis = 66;

// Light.
inline constexpr int kSpellCreateFood = 78;
inline constexpr int kSpellGoldenTouch = 79;
inline constexpr int kSpellDayOfTheGods = 83;
inline constexpr int kSpellHourOfPower = 85;
inline constexpr int kSpellDivineIntervention = 88;

// The "one, two or three a point" ladder, which four spells across the three
// schools share: Shared Life's hit points to the pool, Telekinesis' strength,
// Create Food's days per ten points, and Light's own reach. All four say the
// same in their rows. `observed` at 0x4270f7, 0x427b45 and 0x428605.
inline constexpr std::array<int, 3> kOneTwoThreeAPoint{1, 2, 3};

// The "ten plus two, three at expert, whole party at master" ladder, which
// Bless's siblings share with Body's Speed and Power: **Lucky Day**,
// **Meditation** and **Precision**. `observed` at 0x42699c, 0x427368 and
// 0x4275dc, and their rows say exactly this.
[[nodiscard]] inline constexpr int ten_plus_ladder(int points, int rank) noexcept {
    return 10 + (rank >= 1 ? 3 : 2) * points;
}

// Bless and Heroism run for **sixty-four minutes plus five a point**, and
// fifteen a point at master — `900 × points + 3840` seconds against the
// lower ranks' `300 × points + 3840`. Their rows say "1 hour + 5 minutes per
// point" and "+ 15 minutes"; the base is the same sixty-four minutes Haste
// keeps, four more than the hour the prose rounds to. `observed` at
// 0x4266ff and 0x426c3c.
inline constexpr int kBlessBaseMinutes = 64;

[[nodiscard]] inline constexpr std::int64_t bless_minutes(int points, int rank) noexcept {
    const int step = rank >= 2 ? 15 : 5;
    return kBlessBaseMinutes + static_cast<std::int64_t>(step) * points;
}

// Guardian Angel, Meditation and Precision each last an hour a point, the
// same figure Body's and Fire's buffs keep and no row states. `observed` at
// 0x426ba0, 0x427388 and 0x4275fc.
inline constexpr int kSchoolBuffMinutesPerPoint = kMinutesPerHour;

// Day of the Gods and Hour of Power cast everything at two, three or four
// times the caster's Light Magic — and the case adds **ten** on top of the
// multiple, which no row mentions. Day of the Gods runs three hours a point
// at expert and **four** at master. `observed` at 0x428a52..0x428a7b.
inline constexpr std::array<int, 3> kLightMultiple{2, 3, 4};

[[nodiscard]] inline constexpr int day_of_the_gods(int points, int rank) noexcept {
    const int band = rank < 0 ? 0 : rank > 2 ? 2 : rank;
    return kLightMultiple[static_cast<std::size_t>(band)] * points + 10;
}

inline constexpr std::array<int, 3> kDayOfTheGodsHours{2, 3, 4};

// Golden Touch turns a thing into **40, 60 or 80 percent** of its value.
// `observed` at 0x4286c5, and its row says the same.
inline constexpr std::array<int, 3> kGoldenTouchPercent{40, 60, 80};

// Divine Intervention may be cast once, twice or three times a day, exactly
// as Armageddon may. `observed` at 0x428fe5.
inline constexpr std::array<int, 3> kDivineInterventionPerDay{1, 2, 3};

// Healing Touch heals three to seven, five to nine, seven to eleven by rank —
// the one spell in the three schools whose row gives a band rather than a
// formula. `observed` from its row; the case was not decoded to the point of
// confirming it, so this is `inferred` from the prose alone.
inline constexpr std::array<int, 3> kHealingTouchLow{3, 5, 7};
inline constexpr std::array<int, 3> kHealingTouchHigh{7, 9, 11};

[[nodiscard]] inline constexpr int by_school_rank(const std::array<int, 3>& ladder,
                                                  int rank) noexcept {
    return ladder[static_cast<std::size_t>(rank < 0 ? 0 : rank > 2 ? 2 : rank)];
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SPIRIT_MIND_LIGHT_HPP
