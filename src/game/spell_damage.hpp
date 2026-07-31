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

// One spell's damage. The cases take four shapes and the field says which:
//
// - `sides == 0` — no dice at all; the spell is worth `skill + flat`.
// - `dice == -1` — one die per point of skill, plus `flat`.
// - `dice == 0` — **one zero-based roll**: `flat + rand() % sides`, so the
//   band runs `flat` to `flat + sides - 1`. This is the form that was misread
//   first time round, as `flat + 1d(sides)`, and it is one point high.
// - `dice == n` — that many dice of one to `sides`, plus `flat`.
struct SpellDamage {
    int spell = 0;
    int dice = 0;   // -1 the caster's skill, 0 one zero-based roll, n that many
    int sides = 0;  // 0 when the spell rolls nothing at all
    int flat = 0;
};

// All thirty-four, in spell order. `observed` case by case from `0x432af2`
// through `0x432eec`.
inline constexpr std::array<SpellDamage, 34> kSpellDamage{{
    {2, 1, 8, 0},      // Flame Arrow      1d8, "1-8 points"
    {4, -1, 4, 0},     // Fire Bolt        skill d4
    {6, -1, 6, 0},     // Fireball         skill d6
    {7, 0, 0, 6},      // Ring of Fire     skill + 6
    {8, -1, 3, 4},     // Fire Blast       skill d3 + 4
    {9, 0, 0, 8},      // Meteor Shower    skill + 8
    {10, 0, 0, 12},    // Inferno          skill + 12
    {11, -1, 15, 15},  // Incinerate       skill d15 + 15
    {13, 0, 5, 2},     // Static Charge    2..6, "2-6 points"
    {15, 0, 0, 2},     // Sparks           skill + 2
    {18, -1, 8, 0},    // Lightning Bolt   skill d8, "1-8 per point"
    {20, -1, 10, 10},  // Implosion        skill d10 + 10
    {22, 0, 0, 20},    // Starburst        skill + 20
    {24, 2, 3, 0},     // Cold Beam        2d3, "2-6 points"
    {26, -1, 2, 2},    // Poison Spray     skill d2 + 2, "2 plus 1-2 per point"
    {28, -1, 7, 0},    // Ice Bolt         skill d7
    {30, -1, 9, 9},    // Acid Burst       skill d9 + 9
    {32, -1, 2, 12},   // Ice Blast        skill d2 + 12
    {35, 0, 6, 3},     // Magic Arrow      3..8, "3-8 points"
    {37, -1, 3, 5},    // Deadly Swarm     skill d3 + 5, "5 plus 1-3 per point"
    {39, -1, 5, 0},    // Blades           skill d5
    {41, -1, 8, 0},    // Rock Blast       skill d8
    {43, 0, 0, 20},    // Death Blossom    skill + 20
    {45, 1, 6, 0},     // Spirit Arrow     1d6, "1-6 points"
    {58, -1, 2, 5},    // Mind Blast       skill d2 + 5, "5 plus 1-2 per point"
    {65, -1, 12, 12},  // Psychic Shock    skill d12 + 12
    {70, -1, 2, 8},    // Harm             skill d2 + 8, "8 plus 1-2 per point"
    {76, -1, 5, 30},   // Flying Fist      skill d5 + 30
    {82, -1, 16, 16},  // Destroy Undead   skill d16 + 16
    {84, 0, 0, 25},    // Prismatic Light  skill + 25
    {87, -1, 20, 20},  // Sun Ray          skill d20 + 20, "20 plus 1-20 per point"
    {90, -1, 10, 25},  // Toxic Cloud      skill d10 + 25
    {92, -1, 6, 6},    // Shrapmetal       skill d6 + 6
    {97, -1, 25, 0},   // Dragon Breath    skill d25, "1-25 per point"
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
    const auto sides = static_cast<std::uint64_t>(row->sides);
    if (row->dice == 0) {
        return row->flat + static_cast<int>(next() % sides);
    }
    const int count = row->dice < 0 ? (skill < 0 ? 0 : skill) : row->dice;
    int total = row->flat;
    for (int i = 0; i < count; ++i) {
        total += static_cast<int>(next() % sides) + 1;
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
    if (row->dice < 0) {
        flat = {row->flat, row->flat};
        per_skill = {1, row->sides};
        return true;
    }
    if (row->dice == 0) {
        flat = {row->flat, row->flat + row->sides - 1};
        return true;
    }
    // Several fixed dice, flattened to their band: the engine's roller takes
    // a range, and only Cold Beam's 2d3 uses this form.
    flat = {row->flat + row->dice, row->flat + row->dice * row->sides};
    return true;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SPELL_DAMAGE_HPP
