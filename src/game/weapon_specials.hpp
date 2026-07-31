#ifndef STARHAVEN_GAME_WEAPON_SPECIALS_HPP
#define STARHAVEN_GAME_WEAPON_SPECIALS_HPP

// What a weapon's special enchantment adds to a blow, out of MM6.exe.
//
// After a hit lands, the routine at `0x430f87` walks every slot the striker
// wears, skips whatever the flag byte at `+0x13c` marks broken, and switches
// on the item's **special-enchantment dword at `+0xc`** through a jump table
// at `0x431b0c` — kinds 4..46, selected by a byte table at `0x431b48`. Each
// case rolls its own dice, subtracts them from the target's hit points at
// `+0x28`, and records a damage-type id in a list the caller then shows.
// `observed`
//
// Nothing in the game's own special-bonus table carries these numbers: it
// gives each row a name, a class letter and a price and stops. The dice here
// are the executable's alone.

#include <array>
#include <cstddef>
#include <string_view>

namespace starhaven::game {

// The damage types the cases record beside their rolls: cold, electricity,
// poison and fire, by the ids they write. `observed`
enum class SpecialElement : int { None = 0, Cold = 5, Electric = 6, Poison = 7, Fire = 8 };

// One special's rider: the type it answers to and the inclusive band it
// rolls. A band of the same low and high is a flat amount, which is how the
// three poison specials are written.
struct SpecialRider {
    int special = 0;
    SpecialElement element = SpecialElement::None;
    int low = 0;
    int high = 0;
    std::string_view name;
};

// The thirteen that add damage, in the table's own numbering. `observed`
// — the three cold specials at `0x4310a3`, `0x4310ce` and `0x4310ef`, the
// three electric at `0x43111a`, `0x431145` and `0x431166`, the three fire at
// `0x431187`, `0x4311a6` and `0x4311c4`, the three poison at `0x4311e2`,
// `0x4311f3` and `0x431204`, and of The Dragon at `0x431215`.
inline constexpr std::array<SpecialRider, 13> kSpecialRiders{{
    {4, SpecialElement::Cold, 3, 4, "of Cold"},
    {5, SpecialElement::Cold, 6, 8, "of Frost"},
    {6, SpecialElement::Cold, 9, 12, "of Ice"},
    {7, SpecialElement::Electric, 2, 5, "of Sparks"},
    {8, SpecialElement::Electric, 4, 10, "of Lightning"},
    {9, SpecialElement::Electric, 6, 15, "of Thunderbolts"},
    {10, SpecialElement::Fire, 1, 5, "of Fire"},
    {11, SpecialElement::Fire, 2, 12, "of Flame"},
    {12, SpecialElement::Fire, 16, 18, "of Infernos"},
    {13, SpecialElement::Poison, 5, 5, "of Poison"},
    {14, SpecialElement::Poison, 8, 8, "of Venom"},
    {15, SpecialElement::Poison, 12, 12, "of Acid"},
    {46, SpecialElement::Fire, 10, 20, "of The Dragon"},
}};

[[nodiscard]] inline constexpr const SpecialRider* special_rider(int special) noexcept {
    for (const auto& rider : kSpecialRiders) {
        if (rider.special == special) {
            return &rider;
        }
    }
    return nullptr;
}

// Two specials take a fifth of the blow back as health for the striker
// instead of adding damage: **16 Vampiric** and **41 of Darkness** share one
// case with the artifact **400, Mordred**, which drains the same way. The
// share is the dealt damage times `0x66666667 >> 33`, which is a fifth, and
// it is capped at the striker's maximum. `observed` at `0x430ff2`.
inline constexpr int kVampiricSpecial = 16;
inline constexpr int kDarknessSpecial = 41;
inline constexpr int kMordred = 400;
inline constexpr int kVampiricShare = 5;

[[nodiscard]] inline constexpr bool special_drains(int special, int item_id) noexcept {
    return special == kVampiricSpecial || special == kDarknessSpecial || item_id == kMordred;
}

[[nodiscard]] inline constexpr int vampiric_gain(int damage) noexcept {
    return damage / kVampiricShare;
}

// Two artifacts the routine names by id rather than by enchantment, each
// adding a flat amount: **415 Hades** twenty, **416 Ares** thirty.
// `observed` at `0x430fc9` and `0x430fda`.
inline constexpr int kHades = 415;
inline constexpr int kAres = 416;

[[nodiscard]] inline constexpr int artifact_extra(int item_id) noexcept {
    return item_id == kHades ? 20 : item_id == kAres ? 30 : 0;
}

// Which resistance column answers a special's element, by the name the
// monster table's columns use.
[[nodiscard]] inline constexpr std::string_view element_column(SpecialElement element) noexcept {
    switch (element) {
        case SpecialElement::Cold:
            return "Cold";
        case SpecialElement::Electric:
            return "Elec";
        case SpecialElement::Poison:
            return "Poison";
        case SpecialElement::Fire:
            return "Fire";
        default:
            return {};
    }
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_WEAPON_SPECIALS_HPP
