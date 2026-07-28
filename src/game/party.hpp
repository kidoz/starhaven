#ifndef STARHAVEN_GAME_PARTY_HPP
#define STARHAVEN_GAME_PARTY_HPP

// The four people the game is about.
//
// Almost everything shown on a character sheet is in the design tables: the
// field names and their order are `stats.txt`'s rows, the class descriptions
// are `Class.txt`, the skills are `SkillDes.txt`, the interface's words are
// `GLOBAL.TXT`, and the given names are `npcnames.txt`. What is **not** in any
// table is what a character starts with — the original keeps that in its
// executable — so the numbers below are this engine's, and are marked where
// they are defined.

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/name_table.hpp"
#include "core/data/spell_stats.hpp"
#include "core/random.hpp"

namespace starhaven::game {

// The seven numbers a character is made of, in the order `stats.txt` lists
// them. `observed`
enum class Attribute : std::uint8_t {
    Might,
    Intellect,
    Personality,
    Endurance,
    Accuracy,
    Speed,
    Luck,
    Count,
};

inline constexpr std::size_t kAttributeCount = static_cast<std::size_t>(Attribute::Count);

// The twelve portrait families in `icons.lod`: eight male, then four female.
// Each has 53 frames of 59x79 pixels. `observed`
inline constexpr int kFaceCount = 12;
inline constexpr int kMaleFaceCount = 8;
inline constexpr int kPortraitFrameCount = 53;

// The archive entry for one face and frame: face 0 frame 1 is "MaleA01".
// Frames are numbered from 1, and frame 1 is the neutral one this engine
// draws; what the other 52 are for is `unknown`.
[[nodiscard]] inline std::string portrait_entry(int face, int frame = 1) {
    if (face < 0 || face >= kFaceCount || frame < 1 || frame > kPortraitFrameCount) {
        return {};
    }
    const bool female = face >= kMaleFaceCount;
    const char letter = static_cast<char>('A' + (female ? face - kMaleFaceCount : face));
    std::string name = female ? "Girl" : "Male";
    name += letter;
    name += static_cast<char>('0' + frame / 10);
    name += static_cast<char>('0' + frame % 10);
    return name;
}

[[nodiscard]] inline bool face_is_female(int face) noexcept {
    return face >= kMaleFaceCount;
}

// The places a character can wear something. `ITEMS.TXT`'s equip type says
// which slot an item belongs in, so the list is the table's, not this
// engine's — the one judgement here is that a two-handed weapon occupies the
// same hand a one-handed one does. `inferred`
enum class Slot : std::uint8_t {
    Weapon,
    Shield,
    Armor,
    Helm,
    Belt,
    Cloak,
    Gauntlets,
    Boots,
    Ring,
    Amulet,
    Count,
};

inline constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);

// Which slot an item goes in, or Count when it is not worn at all.
[[nodiscard]] inline Slot slot_for(data::ItemEquipType type) noexcept {
    switch (type) {
    case data::ItemEquipType::Weapon:
    case data::ItemEquipType::TwoHandedWeapon:
    case data::ItemEquipType::Missile:
        return Slot::Weapon;
    case data::ItemEquipType::Shield:
        return Slot::Shield;
    case data::ItemEquipType::Armor:
        return Slot::Armor;
    case data::ItemEquipType::Helm:
        return Slot::Helm;
    case data::ItemEquipType::Belt:
        return Slot::Belt;
    case data::ItemEquipType::Cloak:
        return Slot::Cloak;
    case data::ItemEquipType::Gauntlets:
        return Slot::Gauntlets;
    case data::ItemEquipType::Boots:
        return Slot::Boots;
    case data::ItemEquipType::Ring:
        return Slot::Ring;
    case data::ItemEquipType::Amulet:
        return Slot::Amulet;
    default:
        return Slot::Count;
    }
}

[[nodiscard]] inline std::string_view slot_name(Slot slot) noexcept {
    static constexpr std::array<std::string_view, kSlotCount> kNames{
        "weapon", "shield", "armour", "helm", "belt", "cloak", "gloves", "boots", "ring", "amulet"};
    const auto i = static_cast<std::size_t>(slot);
    return i < kNames.size() ? kNames[i] : std::string_view{};
}

// One character.
struct Character {
    std::string name;
    std::string class_name;  // a heading in Class.txt
    int face = 0;

    int level = 1;
    int experience = 0;
    int age = 18;

    std::array<int, kAttributeCount> attributes{};

    int hit_points = 0;
    int max_hit_points = 0;
    int spell_points = 0;
    int max_spell_points = 0;
    int armor_class = 0;
    int skill_points = 0;

    // What is worn, as ITEMS.TXT ids; zero means the slot is empty.
    std::array<int, kSlotCount> equipped{};

    // Fire, Electricity, Cold, Poison and Magic, in the order `stats.txt`
    // lists them and `MONSTERS.TXT` carries them. A starting character has
    // none of any. `inferred`
    std::array<int, data::kResistanceCount> resistances{};

    // What fountains and potions lay on top, in the tables' own amounts:
    // temporary attribute points, armour and resistances that last until the
    // party rests (the "until" is this engine's, the amounts the tables'),
    // and named conditions with the sheet's own hours, kept as the minute
    // they wear off.
    // The spells this character has learned from books, as Spells.txt ids.
    std::set<int> known_spells;

    // Poisoned, at the level the tables name (Poison1..3); zero is well.
    // What poison does over time is this engine's and marked where it ticks.
    int poisoned = 0;

    std::array<int, kAttributeCount> temp_attributes{};
    std::array<int, data::kResistanceCount> temp_resistances{};
    int temp_armor = 0;
    std::int64_t haste_until = 0;
    std::int64_t bless_until = 0;
    std::int64_t heroism_until = 0;
    std::int64_t stone_skin_until = 0;

    [[nodiscard]] int attribute(Attribute a) const noexcept {
        return attributes[static_cast<std::size_t>(a)] +
               temp_attributes[static_cast<std::size_t>(a)];
    }

    // Drop everything that lasts only until a rest.
    void rest_expires() noexcept {
        temp_attributes.fill(0);
        temp_resistances.fill(0);
        temp_armor = 0;
    }
};

// How an attribute reads on the sheet: MM6 shows a value and the game applies
// a bonus derived from it. The bonus table is in the original's executable, so
// this curve is this engine's — one point either side of 15, widening as the
// value climbs. `inferred`
[[nodiscard]] inline int attribute_bonus(int value) noexcept {
    if (value < 5) {
        return -3;
    }
    if (value < 10) {
        return -2;
    }
    if (value < 15) {
        return -1;
    }
    if (value < 20) {
        return 0;
    }
    return 1 + (value - 20) / 5;
}

// The four classes a party starts as. The names are `Class.txt` headings, and
// which four is this engine's choice: the table lists all eighteen with their
// promotions and says nothing about starting parties. `inferred`
inline constexpr std::array<std::string_view, 4> kStartingClasses{"Knight", "Paladin", "Archer",
                                                                  "Cleric"};

// Which of those learn spells, and so start with spell points. `inferred`
[[nodiscard]] inline bool casts_spells(std::string_view class_name) noexcept {
    return class_name != "Knight";
}

// Build a starting party.
//
// The names come from `npcnames.txt`, the game's own list, picked by the seed
// so a party is reproducible. Everything numeric is this engine's.
[[nodiscard]] inline std::array<Character, 4> make_party(const data::NameTable& names,
                                                         std::uint32_t seed) {
    std::array<Character, 4> party;
    Mm6Random random{seed};

    for (std::size_t i = 0; i < party.size(); ++i) {
        Character& c = party[i];
        c.class_name = std::string(kStartingClasses[i]);
        c.face = static_cast<int>(random.next() % kFaceCount);
        c.name = std::string(names.name(face_is_female(c.face), random.next()));

        for (std::size_t a = 0; a < kAttributeCount; ++a) {
            // 11 to 20, which is the range the original's own descriptions
            // treat as ordinary for a starting character. `inferred`
            c.attributes[a] = 11 + static_cast<int>(random.next() % 10);
        }
        c.max_hit_points = 20 + attribute_bonus(c.attribute(Attribute::Endurance)) * 2;
        c.hit_points = c.max_hit_points;
        if (casts_spells(c.class_name)) {
            c.max_spell_points = 10 + attribute_bonus(c.attribute(Attribute::Intellect)) * 2;
            // Every caster starts knowing First Aid; the choice of that one
            // spell is this engine's. `inferred`
            c.known_spells.insert(68);
        }
        c.spell_points = c.max_spell_points;
        c.armor_class = attribute_bonus(c.attribute(Attribute::Speed));
        c.skill_points = 0;
    }
    return party;
}

// The field names a character sheet shows, which are `stats.txt`'s own rows in
// its own order: the seven attributes, then hit points, armour class and the
// rest. Returns the table's row text so nothing is spelled twice.
[[nodiscard]] inline std::string_view stat_label(const data::DescriptionTable& stats,
                                                 std::size_t row) {
    const auto& entries = stats.entries();
    return row < entries.size() ? std::string_view(entries[row].name) : std::string_view{};
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_PARTY_HPP
