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
#include <map>
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

    // What is worn, as ITEMS.TXT ids; zero means the slot is empty. The
    // parallel arrays carry each piece's rolled enchantment: the standard
    // bonus row and strength, or the special bonus row.
    std::array<int, kSlotCount> equipped{};
    std::array<int, kSlotCount> worn_standard{};
    std::array<int, kSlotCount> worn_strength{};
    std::array<int, kSlotCount> worn_special{};

    // What the worn enchantments add, recomputed by the shell each frame
    // from the bonus tables so nothing double-applies.
    std::array<int, kAttributeCount> gear_attributes{};
    std::array<int, data::kResistanceCount> gear_resistances{};

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

    // Skill points held, by SKILLDES.TXT heading. What each grants is the
    // table's own effect line; see src/game/skills.hpp.
    std::map<std::string, int> skills;

    // Poisoned or diseased, at the level the tables name (Poison1..3,
    // Disease1..3); zero is well. What they do over time is this engine's
    // and marked where it ticks. `affliction` is a further condition the
    // monster column named — Asleep, Afraid, Curse — carried by name and
    // shown, its mechanics not yet built.
    int poisoned = 0;
    int diseased = 0;
    std::string affliction;

    // When each landed, in clock minutes: the cures all warn of a "point of
    // no return", so a condition's age is what they check against.
    std::int64_t poisoned_minute = 0;
    std::int64_t diseased_minute = 0;
    std::int64_t affliction_minute = 0;

    // What has been broken on the body, slot by slot: a broken weapon
    // swings as a fist and broken armour counts for nothing.
    std::array<bool, kSlotCount> equipped_broken{};

    std::array<int, kAttributeCount> temp_attributes{};
    std::array<int, data::kResistanceCount> temp_resistances{};
    int temp_armor = 0;
    std::int64_t haste_until = 0;
    std::int64_t bless_until = 0;
    std::int64_t heroism_until = 0;
    std::int64_t stone_skin_until = 0;

    [[nodiscard]] int attribute(Attribute a) const noexcept {
        return attributes[static_cast<std::size_t>(a)] +
               temp_attributes[static_cast<std::size_t>(a)] +
               gear_attributes[static_cast<std::size_t>(a)];
    }

    // The conditions' teeth, by this engine's reading of the tables' words.
    // Dead is past unconscious: a blow that lands on a character already at
    // zero kills them, and only a temple that treats the dead brings them
    // back. Asleep and Paralyze act on nobody's behalf; the afflicted stand
    // there until struck awake, cured, or rested. All `inferred` — the
    // column names the conditions, not their rules.
    [[nodiscard]] bool dead() const noexcept { return affliction == "Dead"; }
    [[nodiscard]] bool can_act() const noexcept {
        return hit_points > 0 && affliction != "Asleep" && affliction != "Paralyze" && !dead();
    }
    // Afraid keeps their feet but not their nerve: they will not swing.
    // "Affraid" is the table's own spelling.
    [[nodiscard]] bool afraid() const noexcept { return affliction == "Affraid"; }

    // Drop everything that lasts only until a rest, and let the night clear
    // what a night plausibly clears: sleep, fear, drink and weakness. Curse,
    // madness, paralysis and death outlast it. `inferred`
    void rest_expires() noexcept {
        temp_attributes.fill(0);
        temp_resistances.fill(0);
        temp_armor = 0;
        if (affliction == "Asleep" || affliction == "Affraid" || affliction == "Drunk" ||
            affliction == "Weak") {
            affliction.clear();
        }
    }
};

// Which of the 53 portrait frames a condition shows. The numbers were read
// off the sheet itself — frame 8 is the green face, 12 the stone-grey one,
// 33 blacked out, 35 slumped shut-eyed, 36 a corpse, 2 a wince — an
// observation of the shipped art rather than any table, and marked so.
// `inferred` Reproduce with `view_bitmap icons.lod MaleA08 --dump`.
[[nodiscard]] inline int portrait_frame_of(const Character& who, bool wincing) noexcept {
    if (who.dead() || who.affliction == "Eradicated") {
        return 36;
    }
    if (who.affliction == "Stone") {
        return 12;
    }
    if (who.hit_points <= 0) {
        return 33;
    }
    if (who.affliction == "Asleep") {
        return 35;
    }
    if (wincing) {
        return 2;
    }
    if (who.poisoned > 0) {
        return 8;
    }
    return 1;
}

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

// The six a new character may be: `Class.txt`'s every third heading, the
// rows the promotion prose names as starting points. `observed` for the
// headings, `inferred` for reading the other twelve as promotions only.
inline constexpr std::array<std::string_view, 6> kBaseClasses{"Knight", "Cleric", "Sorcerer",
                                                              "Paladin", "Archer", "Druid"};

// Which of those learn spells, and so start with spell points. `inferred`
[[nodiscard]] inline bool casts_spells(std::string_view class_name) noexcept {
    return class_name != "Knight";
}

// Derive what the class and the rolled attributes decide: hit points, spell
// points, armour and the starting spell. Rerolling a character at creation
// runs this again; every number is this engine's. `inferred`
inline void derive_start(Character& c) {
    c.max_hit_points = 20 + attribute_bonus(c.attribute(Attribute::Endurance)) * 2;
    // One weapon skill at one point, by class; which is this engine's
    // reading of the class prose (see skills.hpp for the choices).
    c.skills.clear();
    c.skills[c.class_name == "Knight"                                  ? "Sword"
             : c.class_name == "Paladin" || c.class_name == "Cleric"   ? "Mace"
             : c.class_name == "Archer"                                ? "Bow"
                                                                       : "Staff"] = 1;
    c.hit_points = c.max_hit_points;
    c.known_spells.clear();
    c.max_spell_points = 0;
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

// Roll the seven attributes: 11 to 20, which is the range the original's own
// descriptions treat as ordinary for a starting character. `inferred`
inline void roll_attributes(Character& c, Mm6Random& random) {
    for (std::size_t a = 0; a < kAttributeCount; ++a) {
        c.attributes[a] = 11 + static_cast<int>(random.next() % 10);
    }
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
        roll_attributes(c, random);
        derive_start(c);
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
