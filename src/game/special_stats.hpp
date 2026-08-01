#ifndef STARHAVEN_GAME_SPECIAL_STATS_HPP
#define STARHAVEN_GAME_SPECIAL_STATS_HPP

// What the *other* special enchantments do, out of MM6.exe's stat getter.
//
// Thirteen specials add damage after a blow (see weapon_specials.hpp) and
// three more were met one at a time in unrelated routines — "of Recovery"
// inside the recovery drain, "of Swiftness" inside the strike. The question
// this answers is whether the remaining forty-odd are handled the same
// scattered way or by one walk. **There is one walk**, and it is in the
// stat getter: at `0x4831df` the bonused getter steps the equipped items,
// reads each one's special at `+0xc`, and switches on it — ids 1..57, a
// byte selector at `0x4837b8` into a jump table at `0x48376c`. `observed`
//
// But the walk is deliberately narrow. **Thirty-nine of the fifty-seven —
// ids 3..41 — fall to the do-nothing case**, because those are exactly the
// ones handled elsewhere: the elemental damage riders, Vampiric, of
// Darkness, of Recovery. Only eighteen contribute to a stat, and each case
// is the same shape: test which stat is being asked for, add a fixed
// amount. So the answer is both — one walk for the stat bonuses, and
// checks where they matter for everything else.

#include <array>
#include <cstddef>
#include <string_view>

namespace starhaven::game {

// The stat ids the getter dispatches on, in `stats.txt`'s own order for the
// seven attributes. Ids 10..13 are the resistances "of Protection" answers
// for; 7, 8 and 9 are named below.
enum class StatId : int {
    Might = 0,
    Intellect = 1,
    Personality = 2,
    Endurance = 3,
    Accuracy = 4,
    Speed = 5,
    Luck = 6,
    // **Confirmed, and no longer a fit.** Each of the three getters pushes
    // its own id and then reads that stat's own table: the maximum-hit-point
    // getter at `0x482020` pushes **7** and indexes the class hit-point base
    // at `0x4c2630`; the maximum-spell-point getter at `0x482190` pushes
    // **8** and indexes the class spell-point base at `0x4c2638`; the
    // armour-class getter at `0x482840` pushes **9** and adds the Speed row
    // of the attribute ladder. `observed`
    HitPoints = 7,
    SpellPoints = 8,
    ArmorClass = 9,
};

// One special's contribution: the stats it answers for and what it adds.
struct SpecialStat {
    int special = 0;
    int amount = 0;
    std::array<int, 4> stats{-1, -1, -1, -1};
    std::string_view name;
};

// The eighteen that reach a stat. `observed`, case by case, from `0x482f86`
// through `0x483311`.
inline constexpr std::array<SpecialStat, 18> kSpecialStats{{
    {1, 10, {10, 11, 12, 13}, "of Protection"},
    {2, 10, {0, 1, 2, 3}, "of The Gods"},  // and 4, 5, 6: the whole run 0..6
    {42, 1, {-1, -1, -1, -1}, "of Doom"},  // every stat, unconditionally
    {43, 10, {3, 7, -1, -1}, "of Earth"},  // Endurance and hit points
    {44, 10, {7, -1, -1, -1}, "of Life"},  // hit points
    {45, 5, {4, 5, -1, -1}, "Rogues"},
    {46, 25, {0, -1, -1, -1}, "of The Dragon"},
    {47, 10, {8, -1, -1, -1}, "of The Eclipse"},
    {48, 5, {9, -1, -1, -1}, "of The Golem"},
    {49, 10, {6, 1, -1, -1}, "of The Moon"},
    {50, 10, {8, -1, -1, -1}, "of The Phoenix"},
    {51, 10, {5, 1, 8, -1}, "of The Sky"},
    {52, 10, {3, 4, -1, -1}, "of The Stars"},
    {53, 10, {0, 2, -1, -1}, "of The Sun"},
    {54, 15, {3, -1, -1, -1}, "of The Troll"},
    {55, 15, {6, -1, -1, -1}, "of The Unicorn"},
    {56, 5, {0, 3, -1, -1}, "Warriors"},
    {57, 5, {1, 2, -1, -1}, "Wizards"},
}};

// "of Doom" adds its point to whatever is asked; every other case tests the
// stat first.
inline constexpr int kOfDoom = 42;

// "of The Gods" answers for the whole run of seven attributes, which the
// table above cannot spell in four slots.
inline constexpr int kOfTheGods = 2;

// What a worn special adds to one stat, or nothing.
[[nodiscard]] inline constexpr int special_stat_bonus(int special, int stat) noexcept {
    for (const auto& row : kSpecialStats) {
        if (row.special != special) {
            continue;
        }
        if (special == kOfDoom) {
            return row.amount;
        }
        if (special == kOfTheGods) {
            return stat >= 0 && stat <= 6 ? row.amount : 0;
        }
        for (const int answered : row.stats) {
            if (answered >= 0 && answered == stat) {
                return row.amount;
            }
        }
        return 0;
    }
    return 0;
}

// Whether a special reaches the sheet at all, as against being handled where
// it matters.
[[nodiscard]] inline constexpr bool special_reaches_stats(int special) noexcept {
    for (const auto& row : kSpecialStats) {
        if (row.special == special) {
            return true;
        }
    }
    return false;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SPECIAL_STATS_HPP
