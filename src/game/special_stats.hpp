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

// **The whole id space, counted.** Scanning every `push` that immediately
// precedes a call to one of the three stat getters — `0x483800`, `0x483930`
// and `0x482e80` — gives **ids 0 through 21 and 23, and nothing else**. Id 22
// is never asked for anywhere in the image. `observed`
inline constexpr int kStatIdCount = 24;
inline constexpr int kStatIdNeverAsked = 22;

// **The five resistances are 10, 11, 12, 13 and 23**, not 10..13. One routine
// asks for all five in a single run — `0x47f6a3`, `0x47f6eb`, `0x47f733`,
// `0x47f778`, `0x47f7a8`, evenly spaced — and five is exactly how many
// resistance columns `MONSTERS.TXT` carries. That 23 trails the other four
// rather than continuing them is why the earlier reading stopped at 13.
// `observed` for the run, `inferred` that the run is the resistances.
inline constexpr std::array<int, 5> kResistanceStatIds{10, 11, 12, 13, 23};

// Id **14** follows the seven attributes in the sheet's own walk, at
// `0x47d915`, on the same 32-byte stride that spaces ids 0..6 — so it is
// shown beside them. Which row it is stays `unknown`.
inline constexpr int kStatIdAfterAttributes = 14;

// Ids **15..21** are the derived combat figures. Four of them carry a stored
// term on the character (`+0x1570`..`+0x1577`): 15, 16, 19 and 20. Ids 17, 18
// and 21 are asked for only from inside those getters, which makes them
// ingredients rather than figures of their own. `observed`
inline constexpr std::array<int, 4> kStoredTermStatIds{15, 16, 19, 20};

// **The four holes, read.** The gear getter at `0x482e80` dispatches ids 0..22
// through a 23-byte selector at `0x4836b0` into a table at `0x483690`, and
// the four unnamed ids each get a body of their own. Each says where it takes
// its number from in its first two instructions. `observed`
//
//  * **14** (`0x48332e`) walks all **sixteen** equipment anchors from
//    `+0x1428`, skips anything flagged broken, and adds **5** for each item
//    whose **special enchantment at `+0xC` is 25**. So it is a stat only one
//    special feeds, counted over everything worn.
//  * **17** (`0x483409`) takes the item at the **weapon anchor `+0x142c`**,
//    refuses it unless its equip type at `+0x14` is 2 or less, and sums the
//    row's bytes at `+0x18`, `+0x16` and `+0x15`.
//  * **18** (`0x4834c7`) takes the same item and the same refusal, then
//    branches on the row's **skill group being 5** — the bow — with the
//    off-hand anchor at `+0x1428` empty.
//  * **21** (`0x4835ef`) takes the item at the **next anchor, `+0x1430`**, and
//    reads the same `+0x18` byte id 17 sums.
//
// So 17, 18 and 21 are the wielded weapon's own numbers — two from the hand
// that holds it and one from the hand beside it — which is why they are asked
// for only from inside the attack- and shot-damage getters. `observed` for
// every offset; what the row's `+0x16` and `+0x18` columns are called stays
// `unknown`.
inline constexpr int kGearAnchorsFirst = 0x1428;
inline constexpr int kGearAnchorCount = 16;
inline constexpr int kWeaponAnchor = 0x142c;
inline constexpr int kOffHandAnchor = 0x1430;
inline constexpr int kCountedSpecial = 25;
inline constexpr int kCountedSpecialWorth = 5;
inline constexpr int kBowSkillGroup = 5;

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
