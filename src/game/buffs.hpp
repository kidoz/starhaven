#ifndef STARHAVEN_GAME_BUFFS_HPP
#define STARHAVEN_GAME_BUFFS_HPP

// The party's spell buffs, on the executable's own array.
//
// `0x47d170` clears it and gives the shape away: **sixteen records of
// sixteen bytes** starting at `0x908e34`, each an eight-byte expiry at `+0`,
// a **power** word at `+8`, a **skill** word at `+0xa`, and two flag bytes.
// `observed`
//
// Which spell owns which slot was read from the `mov ecx, <slot>` before
// each call to the setter at `0x44a970`:
//
// | slot | spell | at |
// | --- | --- | --- |
// | 0 | Protection from Fire | `0x423784` |
// | 1 | Protection from Cold | `0x425048` |
// | 2 | Protection from Electricity | `0x42444c` |
// | 3 | Protection from Magic | `0x426136` |
// | 4 | Protection from Poison | `0x427f5e` |
// | 5 | (only Day of Protection) | `0x42978a` |
// | 6 | Water Walk | `0x4254a8` |
// | 7 | Fly | `0x424a98` |
// | 8 | Guardian Angel | `0x426c06` |
// | 10 | Wizard Eye | `0x424382` |
// | 11 | Torch Light | `0x4230c6` |
//
// **Day of Protection writes seven of them in a row** (`0x4296aa` through
// `0x42978a`), which is what its row means by protecting against everything.
//
// And the stat getter reads the *power* of the first five: stat id 10 is
// slot 0, 11 slot 2, 12 slot 1, 13 slot 4, 23 slot 3. So the five ids are
// **Fire, Electricity, Cold, Poison and Magic** — `MONSTERS.TXT`'s own
// column order — and that "of Protection" adds ten to 10..13 and nothing to
// 23 is the confirmation: the four elements, not magic. `observed`

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace starhaven::game {

// The slots that have a name. The array holds sixteen; five are unused by
// anything read so far.
enum class PartyBuff : std::size_t {
    ProtectionFromFire = 0,
    ProtectionFromCold = 1,
    ProtectionFromElectricity = 2,
    ProtectionFromMagic = 3,
    ProtectionFromPoison = 4,
    DayOfProtection = 5,
    WaterWalk = 6,
    Fly = 7,
    GuardianAngel = 8,
    WizardEye = 10,
    TorchLight = 11,
};

inline constexpr std::size_t kPartyBuffCount = 16;

// The five the stat getter reads, in `MONSTERS.TXT`'s column order — which
// is also `data::Resistance`'s.
inline constexpr std::array<PartyBuff, 5> kResistanceBuffs{
    PartyBuff::ProtectionFromFire, PartyBuff::ProtectionFromElectricity,
    PartyBuff::ProtectionFromCold, PartyBuff::ProtectionFromPoison,
    PartyBuff::ProtectionFromMagic};

// Which slot a spell fills, or none.
[[nodiscard]] inline constexpr int buff_slot_of_spell(int spell_id) noexcept {
    switch (spell_id) {
        case 3:
            return static_cast<int>(PartyBuff::ProtectionFromFire);
        case 25:
            return static_cast<int>(PartyBuff::ProtectionFromCold);
        case 14:
            return static_cast<int>(PartyBuff::ProtectionFromElectricity);
        case 36:
            return static_cast<int>(PartyBuff::ProtectionFromMagic);
        case 69:
            return static_cast<int>(PartyBuff::ProtectionFromPoison);
        case 27:
            return static_cast<int>(PartyBuff::WaterWalk);
        case 21:
            return static_cast<int>(PartyBuff::Fly);
        case 50:
            return static_cast<int>(PartyBuff::GuardianAngel);
        case 12:
            return static_cast<int>(PartyBuff::WizardEye);
        case 1:
            return static_cast<int>(PartyBuff::TorchLight);
        default:
            return -1;
    }
}

// The seven Day of Protection lays at once, in the order its case writes
// them. `observed` at 0x4296aa..0x42978a.
inline constexpr std::array<int, 7> kDayOfProtectionSlots{4, 0, 1, 3, 2, 10, 5};

// One slot, as the record holds it.
struct BuffSlot {
    std::int64_t until = 0;  // world minute it lapses; 0 is empty
    int power = 0;
    int skill = 0;
};

// The array itself. A cast overwrites its slot rather than stacking, which
// is what a single record per slot can do.
class PartyBuffs {
public:
    void cast(int slot, std::int64_t until, int power, int skill) noexcept {
        if (slot < 0 || slot >= static_cast<int>(kPartyBuffCount)) {
            return;
        }
        slots_[static_cast<std::size_t>(slot)] = {until, power, skill};
    }

    void cast(PartyBuff slot, std::int64_t until, int power, int skill) noexcept {
        cast(static_cast<int>(slot), until, power, skill);
    }

    [[nodiscard]] bool active(int slot, std::int64_t now) const noexcept {
        return slot >= 0 && slot < static_cast<int>(kPartyBuffCount) &&
               slots_[static_cast<std::size_t>(slot)].until > now;
    }

    [[nodiscard]] bool active(PartyBuff slot, std::int64_t now) const noexcept {
        return active(static_cast<int>(slot), now);
    }

    // What the slot is worth now, or nothing when it has lapsed.
    [[nodiscard]] int power(int slot, std::int64_t now) const noexcept {
        return active(slot, now) ? slots_[static_cast<std::size_t>(slot)].power : 0;
    }

    [[nodiscard]] int power(PartyBuff slot, std::int64_t now) const noexcept {
        return power(static_cast<int>(slot), now);
    }

    [[nodiscard]] const BuffSlot& at(std::size_t slot) const noexcept { return slots_[slot]; }
    [[nodiscard]] BuffSlot& at(std::size_t slot) noexcept { return slots_[slot]; }

    // What the protections add to one of the five resistance columns, by the
    // order `MONSTERS.TXT` writes them.
    [[nodiscard]] int resistance(std::size_t column, std::int64_t now) const noexcept {
        return column < kResistanceBuffs.size() ? power(kResistanceBuffs[column], now) : 0;
    }

    void clear() noexcept { slots_ = {}; }

private:
    std::array<BuffSlot, kPartyBuffCount> slots_{};
};

// The name each named slot shows, for the sheet and the save file.
[[nodiscard]] inline constexpr std::string_view buff_name(int slot) noexcept {
    switch (static_cast<PartyBuff>(slot)) {
        case PartyBuff::ProtectionFromFire:
            return "Protection from Fire";
        case PartyBuff::ProtectionFromCold:
            return "Protection from Cold";
        case PartyBuff::ProtectionFromElectricity:
            return "Protection from Electricity";
        case PartyBuff::ProtectionFromMagic:
            return "Protection from Magic";
        case PartyBuff::ProtectionFromPoison:
            return "Protection from Poison";
        case PartyBuff::DayOfProtection:
            return "Day of Protection";
        case PartyBuff::WaterWalk:
            return "Water Walk";
        case PartyBuff::Fly:
            return "Fly";
        case PartyBuff::GuardianAngel:
            return "Guardian Angel";
        case PartyBuff::WizardEye:
            return "Wizard Eye";
        case PartyBuff::TorchLight:
            return "Torch Light";
        default:
            return {};
    }
}

// The character's own array, found the same two ways. `0x47d170` walks the
// four party records in strides of `0x161c` and clears, for each one, an
// item array of **28-byte** records at `+0x144` and a buff array of
// **sixteen 16-byte records at `+0x1268`**. `observed`
//
// Which slot a spell takes was read from the `lea ecx, [reg + N]` before its
// call to the setter, and four of them check out against their own rows:
//
// | slot | offset | spell | the row says |
// | --- | --- | --- | --- |
// | 2 | `+0x1288` | Haste | — |
// | 6 | `+0x12c8` | Meditation | "Increases Intellect **and Personality**" |
// | 7 | `+0x12d8` | Meditation | the second of the pair |
// | 10 | `+0x1308` | Power | "Increases Might **and Endurance**" |
// | 11 | `+0x1318` | Power | the second of the pair |
//
// **The open question is closed: this array *is* the attribute-bonus
// store.** The script-variable table names the attribute fields outright —
// variables 32..37 write `+0x18` through `+0x2c` and 38..44 write `+0x16`
// through `+0x2e`, seven pairs of words four bytes apart — so the stored
// attributes are nowhere near `+0x12b0`. The words the stat getter reads
// there are the **power fields of these records**, at `+8` of each, which is
// why Meditation's slots 6 and 7 land on Intellect and Personality and
// Power's 10 and 11 on Might and Endurance. `observed`
inline constexpr std::size_t kCharacterBuffCount = 16;
inline constexpr int kCharacterBuffBase = 0x1268;

// The whole map, read from the `mov reg, 0x90a1xx` each case loads before
// its call to the setter — the array's own base plus sixteen bytes a slot.
// `observed` at the twelve addresses listed beside them.
//
// | slot | spell | at |
// | --- | --- | --- |
// | 0 | Bless | `0x426759` |
// | 1 | Heroism | `0x426c96` |
// | 2 | Haste | `0x4237eb` |
// | 3 | Shield | `0x4247a3` |
// | 4 | Stone Skin | `0x4261d1` |
// | 5 | Lucky Day | `0x426a9b` |
// | 6, 7 | Meditation | `0x427424`, `0x4274a2` |
// | 8 | Precision | `0x4276db` |
// | 9 | Speed | `0x428222` |
// | 10, 11 | Power | `0x428483`, `0x428501` |
//
// Slots 4 through 11 are the eight whose power words the stat getter reads
// as the attribute bonuses, and every attribute-buffing spell lands on the
// slot for the attribute its own row names: Lucky Day on Luck, Meditation on
// Intellect and Personality, Precision on Accuracy, Speed on Speed, Power on
// Might and Endurance. Two Light spells reach in as well — Day of the Gods
// writes slot 11 (`0x428add`) and Hour of Power slot 1 (`0x428e0c`).
enum class CharacterBuff : std::size_t {
    Bless = 0,
    Heroism = 1,
    Haste = 2,
    Shield = 3,
    StoneSkin = 4,
    LuckyDay = 5,
    MeditationIntellect = 6,
    MeditationPersonality = 7,
    Precision = 8,
    Speed = 9,
    PowerMight = 10,
    PowerEndurance = 11,
};

// Which slot a spell fills on a character, or none.
[[nodiscard]] inline constexpr int character_slot_of_spell(int spell_id) noexcept {
    switch (spell_id) {
        case 46:
            return static_cast<int>(CharacterBuff::Bless);
        case 51:
            return static_cast<int>(CharacterBuff::Heroism);
        case 5:
            return static_cast<int>(CharacterBuff::Haste);
        case 17:
            return static_cast<int>(CharacterBuff::Shield);
        case 38:
            return static_cast<int>(CharacterBuff::StoneSkin);
        case 48:
            return static_cast<int>(CharacterBuff::LuckyDay);
        case 59:
            return static_cast<int>(CharacterBuff::Precision);
        case 73:
            return static_cast<int>(CharacterBuff::Speed);
        default:
            return -1;
    }
}

// The two that take a pair, and the attributes their rows name.
inline constexpr std::array<int, 2> kMeditationSlots{6, 7};
inline constexpr std::array<int, 2> kPowerSlots{10, 11};

// Which stat id each of the eight attribute slots answers for, by the order
// the stat getter reads their power words at `+0x12b0 + 16k`. `observed`
inline constexpr std::array<int, 8> kAttributeBuffStat{9, 6, 1, 2, 4, 5, 0, 3};

[[nodiscard]] inline constexpr int buff_slot_for_stat(int stat) noexcept {
    for (std::size_t i = 0; i < kAttributeBuffStat.size(); ++i) {
        if (kAttributeBuffStat[i] == stat) {
            return static_cast<int>(i) + 4;
        }
    }
    return -1;
}

// A character's sixteen, the same record as the party's.
class CharacterBuffs {
public:
    void cast(std::size_t slot, std::int64_t until, int power) noexcept {
        if (slot < kCharacterBuffCount) {
            slots_[slot] = {until, power, 0};
        }
    }

    void cast(CharacterBuff slot, std::int64_t until, int power) noexcept {
        cast(static_cast<std::size_t>(slot), until, power);
    }

    [[nodiscard]] int power(std::size_t slot, std::int64_t now) const noexcept {
        return slot < kCharacterBuffCount && slots_[slot].until > now ? slots_[slot].power : 0;
    }

    [[nodiscard]] int power(CharacterBuff slot, std::int64_t now) const noexcept {
        return power(static_cast<std::size_t>(slot), now);
    }

    [[nodiscard]] bool active(CharacterBuff slot, std::int64_t now) const noexcept {
        return power(slot, now) > 0 ||
               (static_cast<std::size_t>(slot) < kCharacterBuffCount &&
                slots_[static_cast<std::size_t>(slot)].until > now);
    }

    void clear() noexcept { slots_ = {}; }

private:
    std::array<BuffSlot, kCharacterBuffCount> slots_{};
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_BUFFS_HPP
