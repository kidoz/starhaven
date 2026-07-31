#ifndef STARHAVEN_GAME_SPELL_SWITCH_HPP
#define STARHAVEN_GAME_SPELL_SWITCH_HPP

// What MM6.exe's spell machinery knows about a spell before it runs it.
//
// The per-spell case bodies are read in src/game/body_magic.hpp and
// docs/formats/spell-switch.md. This header carries the two things that sit
// *around* those cases: which spells are aimed at the world, and what a cast
// spends. Both are `observed`.

#include <array>
#include <cstddef>

namespace starhaven::game {

// Whether a spell is thrown at the world rather than laid on the party.
//
// `0x421f70` answers exactly that, for spell ids 2..99, through a byte table
// at `0x421f98` whose two cases return 1 and 0. Its one caller, `0x420b56`,
// reads the active character's **readied spell id at `+0x152f`** and, when
// this says yes, queues action 25 on a click — so the predicate is what
// decides whether a click on the world fires the readied spell.
//
// Exactly **47** of the 99 answer yes, and they are every direct-damage
// spell plus the aimed status spells: Stun, Turn to Stone, Charm, Mass Fear,
// Feeblemind, Dispel Magic, Slow, Destroy Undead, Paralyze, Mass Curse and
// Shrinking Ray. Every id served by the switch's three shared launcher
// bodies is in the list, and fifteen more with cases of their own. Nothing
// in `SPELLS.TXT` states this partition; the table is its only source.
// `observed`
inline constexpr std::array<int, 47> kAimedSpells{
    2,  4,  6,  7,  8,  9,  10, 11, 13, 15, 18, 20, 22, 24, 26, 28,
    30, 32, 34, 35, 37, 39, 41, 42, 43, 44, 45, 58, 61, 62, 63, 65,
    70, 76, 80, 81, 82, 86, 87, 90, 91, 92, 93, 95, 96, 97, 99};

[[nodiscard]] inline constexpr bool spell_is_aimed(int spell_id) noexcept {
    for (const int id : kAimedSpells) {
        if (id == spell_id) {
            return true;
        }
    }
    return false;
}

// A cast is paid for by `0x421f30`, which every case body guards itself
// with: it compares the caster's **spell points at `+0x1418`** against the
// cost, plays sound 209 and refuses when they fall short, and otherwise
// subtracts and returns. The three shared launcher bodies do the same
// subtraction inline rather than calling it. `observed`
inline constexpr int kNotEnoughSpellPointsSound = 209;

// The engine's universal object handle, packed into one dword: the index
// shifted up three bits with a kind in the low three. **Kind 4 is a party
// member, kind 3 an actor.** The launcher stamps a projectile with its
// caster this way (`0x4231bd`), and the queued-message handler that fills
// both recovery counters unpacks the same shape (`0x405c4b`). `observed`
inline constexpr int kHandleKindActor = 3;
inline constexpr int kHandleKindPartyMember = 4;

[[nodiscard]] inline constexpr int packed_handle(int index, int kind) noexcept {
    return (index << 3) | kind;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SPELL_SWITCH_HPP
