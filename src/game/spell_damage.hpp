#ifndef STARHAVEN_GAME_SPELL_DAMAGE_HPP
#define STARHAVEN_GAME_SPELL_DAMAGE_HPP

// What a thrown spell does, out of MM6.exe — the table three attempts missed.
//
// `0x432ad0` takes a spell id, the caster's skill masked to its low six bits,
// and a third word, and returns the damage. It switches on **spell id minus
// two** through a 98-entry byte selector at `0x432f84` into a jump table at
// `0x432ef8`: **34 spells have a case and the other 62 fall to a common
// return of zero**. `observed`
//
// It is reached from the projectile's collision handler, which calls it with
// three fields the launcher stamped on the object: `[obj+0x40]` the spell id,
// `[obj+0x44]` the caster's skill, `[obj+0x48]` the third word. That is what
// the earlier searches were looking for and could not find, because they
// were reading the *monster's* branch of the handler rather than the party's.
//
// Nothing in `SPELLS.TXT` carries these dice. Its prose gives a phrase per
// rank and the engine had been parsing that; this is the arithmetic itself.

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/data/spell_effects.hpp"

namespace starhaven::game {

// One spell's damage. When `sides` is zero the spell rolls nothing and is
// worth `skill + flat`; otherwise it rolls `sides`-sided dice, one of them
// unless `by_skill`, in which case the caster's skill says how many, and
// adds `flat` on top.
struct SpellDamage {
    int spell = 0;
    int sides = 0;
    bool by_skill = false;
    int flat = 0;
};

// All thirty-four, in spell order. `observed` case by case from `0x432af2`
// through `0x432eec`.
inline constexpr std::array<SpellDamage, 34> kSpellDamage{{
    {2, 8, false, 0},      // Flame Arrow      1d8
    {4, 4, true, 0},       // Fire Bolt        skill d4
    {6, 6, true, 0},       // Fireball         skill d6
    {7, 0, false, 6},      // Ring of Fire     skill + 6
    {8, 3, true, 4},       // Fire Blast       skill d3 + 4
    {9, 0, false, 8},      // Meteor Shower    skill + 8
    {10, 0, false, 12},    // Inferno          skill + 12
    {11, 15, true, 15},    // Incinerate       skill d15 + 15
    {13, 5, false, 2},     // Static Charge    1d5 + 2
    {15, 0, false, 2},     // Sparks           skill + 2
    {18, 8, true, 0},      // Lightning Bolt   skill d8
    {20, 10, true, 10},    // Implosion        skill d10 + 10
    {22, 0, false, 20},    // Starburst        skill + 20
    {24, 3, false, 0},     // Cold Beam        1d3
    {26, 2, true, 2},      // Poison Spray     skill d2 + 2
    {28, 7, true, 0},      // Ice Bolt         skill d7
    {30, 9, true, 9},      // Acid Burst       skill d9 + 9
    {32, 2, true, 12},     // Ice Blast        skill d2 + 12
    {35, 6, false, 3},     // Magic Arrow      1d6 + 3
    {37, 3, true, 5},      // Deadly Swarm     skill d3 + 5
    {39, 5, true, 0},      // Blades           skill d5
    {41, 8, true, 0},      // Rock Blast       skill d8
    {43, 0, false, 20},    // Death Blossom    skill + 20
    {45, 6, false, 0},     // Spirit Arrow     1d6
    {58, 2, true, 5},      // Mind Blast       skill d2 + 5
    {65, 12, true, 12},    // Psychic Shock    skill d12 + 12
    {70, 2, true, 8},      // Harm             skill d2 + 8
    {76, 5, true, 30},     // Flying Fist      skill d5 + 30
    {82, 16, true, 16},    // Destroy Undead   skill d16 + 16
    {84, 0, false, 25},    // Prismatic Light  skill + 25
    {87, 20, true, 20},    // Sun Ray          skill d20 + 20
    {90, 10, true, 25},    // Toxic Cloud      skill d10 + 25
    {92, 6, true, 6},      // Shrapmetal       skill d6 + 6
    {97, 25, true, 0},     // Dragon Breath    skill d25
}};

// Armageddon and Dark Containment share the last case, `skill + 50`. They
// sit outside the array only because it is kept in one-entry-per-case order.
inline constexpr int kSpellArmageddonDamage = 50;

[[nodiscard]] inline constexpr const SpellDamage* spell_damage_of(int spell_id) noexcept {
    for (const auto& row : kSpellDamage) {
        if (row.spell == spell_id) {
            return &row;
        }
    }
    return nullptr;
}

[[nodiscard]] inline constexpr bool spell_rolls_damage(int spell_id) noexcept {
    return spell_damage_of(spell_id) != nullptr || spell_id == 98 || spell_id == 99;
}

// Roll it. `next` is any source of unsigned numbers; the executable's own
// loop adds `rand() % sides + 1` once per die and the flat part on top,
// which it also returns when the skill is nothing.
template <typename Next>
[[nodiscard]] inline int roll_spell_damage(int spell_id, int skill, Next&& next) {
    if (spell_id == 98 || spell_id == 99) {
        return skill + kSpellArmageddonDamage;
    }
    const SpellDamage* row = spell_damage_of(spell_id);
    if (row == nullptr) {
        return 0;
    }
    if (row->sides <= 0) {
        return skill + row->flat;
    }
    const int dice = row->by_skill ? (skill < 0 ? 0 : skill) : 1;
    int total = row->flat;
    for (int i = 0; i < dice; ++i) {
        total += static_cast<int>(next() % static_cast<std::uint64_t>(row->sides)) + 1;
    }
    return total;
}

// The same table said in the shape the engine's own strike already takes: a
// flat part rolled once and a part rolled per point of skill. The mapping is
// exact — `skill dN` is a per-skill range of one to N, a lone die is a flat
// range, and the diceless spells are a flat `skill + K`. Returns false when
// the spell has no case, and the table's prose must answer instead.
[[nodiscard]] inline bool traced_damage_ranges(int spell_id, int skill, data::SpellRange& flat,
                                               data::SpellRange& per_skill) noexcept {
    flat = {};
    per_skill = {};
    if (spell_id == 98 || spell_id == 99) {
        flat = {skill + kSpellArmageddonDamage, skill + kSpellArmageddonDamage};
        return true;
    }
    const SpellDamage* row = spell_damage_of(spell_id);
    if (row == nullptr) {
        return false;
    }
    if (row->sides <= 0) {
        flat = {skill + row->flat, skill + row->flat};
        return true;
    }
    if (row->by_skill) {
        flat = {row->flat, row->flat};
        per_skill = {1, row->sides};
        return true;
    }
    flat = {row->flat + 1, row->flat + row->sides};
    return true;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SPELL_DAMAGE_HPP
