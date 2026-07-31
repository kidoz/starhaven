#ifndef STARHAVEN_GAME_CONDITIONS_HPP
#define STARHAVEN_GAME_CONDITIONS_HPP

// The character's conditions, as MM6.exe numbers them.
//
// They are a run of **eight-byte timestamps from `+0x1380`**, one per
// condition, and a condition is "on" when its eight bytes are not zero — no
// counter, no level, just when it was caught. The run reaches at least slot
// 17: the attack-bonus walk falls back to id 17 when it finds nothing.
//
// Three independent readings name them, and they agree.
//
// **From the cure spells.** Each of Body's three timed cures pushes the id
// it lifts beside its cutoff time: Cure Weakness **1**, Cure Poison **6**,
// Cure Disease **7**. `observed` at 0x427e1e, 0x4280ac and 0x428327.
//
// **From the heal.** `0x47fb60` refuses to heal a character whose slot
// **14** or **16** is set — the two a spell cannot touch. `observed`
//
// **From the AI.** `0x404cdf` treats a character as not there at all when
// any of slots **2, 12, 13, 14, 15** or **16** is set, which is the set of
// conditions that take someone out of a fight. `observed`
//
// Those three sets fit one ordering and no other: the six that end a
// character's participation are Asleep and then the run Paralyzed,
// Unconscious, Dead, Stoned, Eradicated; the heal's two refusals are Dead
// and Eradicated; and the cures' 1, 6 and 7 are Weak, Poisoned and Diseased.
// MM6 carries a single level of poison and disease where later games split
// them into three, which is why 6 and 7 sit adjacent.
//
// `observed` for 1, 6, 7, 14 and 16; `inferred` for the rest of the naming,
// which follows from the ordering rather than from an instruction.

#include <array>
#include <cstddef>
#include <string_view>

namespace starhaven::game {

inline constexpr int kConditionCursed = 0;
inline constexpr int kConditionWeakId = 1;
inline constexpr int kConditionAsleep = 2;
inline constexpr int kConditionAfraid = 3;
inline constexpr int kConditionDrunk = 4;
inline constexpr int kConditionInsane = 5;
inline constexpr int kConditionPoisoned = 6;
inline constexpr int kConditionDiseased = 7;
inline constexpr int kConditionParalyzed = 12;
inline constexpr int kConditionUnconscious = 13;
inline constexpr int kConditionDead = 14;
inline constexpr int kConditionStoned = 15;
inline constexpr int kConditionEradicated = 16;
inline constexpr int kConditionZombie = 17;

inline constexpr std::size_t kConditionCount = 18;

// The record's own arithmetic, kept so the offsets can be checked.
inline constexpr int kConditionRunBase = 0x1380;
inline constexpr int kConditionStride = 8;

[[nodiscard]] inline constexpr int condition_offset(int id) noexcept {
    return kConditionRunBase + kConditionStride * id;
}

// The six that take a character out of a fight, by the set the AI tests.
inline constexpr std::array<int, 6> kIncapacitating{
    kConditionAsleep, kConditionParalyzed, kConditionUnconscious,
    kConditionDead,   kConditionStoned,    kConditionEradicated};

// The two a heal refuses.
inline constexpr std::array<int, 2> kBeyondHealing{kConditionDead, kConditionEradicated};

[[nodiscard]] inline constexpr bool incapacitates(int id) noexcept {
    for (const int c : kIncapacitating) {
        if (c == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline constexpr bool beyond_healing(int id) noexcept {
    return id == kConditionDead || id == kConditionEradicated;
}

[[nodiscard]] inline constexpr std::string_view condition_name(int id) noexcept {
    switch (id) {
        case kConditionCursed: return "Cursed";
        case kConditionWeakId: return "Weak";
        case kConditionAsleep: return "Asleep";
        case kConditionAfraid: return "Afraid";
        case kConditionDrunk: return "Drunk";
        case kConditionInsane: return "Insane";
        case kConditionPoisoned: return "Poisoned";
        case kConditionDiseased: return "Diseased";
        case kConditionParalyzed: return "Paralyzed";
        case kConditionUnconscious: return "Unconscious";
        case kConditionDead: return "Dead";
        case kConditionStoned: return "Stoned";
        case kConditionEradicated: return "Eradicated";
        case kConditionZombie: return "Zombie";
        default: return {};
    }
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_CONDITIONS_HPP
