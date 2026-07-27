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
#include <string>
#include <string_view>
#include <vector>

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

    [[nodiscard]] int attribute(Attribute a) const noexcept {
        return attributes[static_cast<std::size_t>(a)];
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
