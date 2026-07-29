#ifndef STARHAVEN_GAME_SKILLS_HPP
#define STARHAVEN_GAME_SKILLS_HPP

// Skills, on `SKILLDES.TXT`'s own terms.
//
// The table lists 31 skills, and beside each skill's prose it states what
// the skill does in short effect lines: "Skill added to Attack Bonus",
// "Skill added to Attack Damage", "Skill added to Armor Class", "Skill adds
// to Hit Points", "Skill adds to Spell Points", "Skill adjusts shop prices
// in your favor". This engine applies exactly the effects those lines name,
// at the first (normal) rank — the expert and master lines are doublings and
// specials beyond this slice. What a point of skill is numerically worth
// where the line names no number — one point of attack bonus per point of
// skill, one percent off a price — is this engine's own and marked below.

#include <cctype>
#include <map>
#include <vector>
#include <string>
#include <string_view>

#include "core/data/spell_stats.hpp"

namespace starhaven::game {

// What one skill's first effect line grants, matched against the table's own
// phrasings. A skill whose line names none of these is carried as prose.
struct SkillEffect {
    bool attack_bonus = false;   // "Skill added to Attack Bonus"
    bool attack_damage = false;  // "Skill added to Attack Damage"
    bool armor_class = false;    // "Skill added to Armor Class"
    bool hit_points = false;     // "Skill adds to Hit Points"
    bool spell_points = false;   // "Skill adds to Spell Points"
    bool shop_prices = false;    // "Skill adjusts shop prices in your favor"
};

// Read a skill's effect lines. `lines` are the description columns of its
// SKILLDES.TXT row, in table order.
[[nodiscard]] inline SkillEffect parse_skill_effect(const std::vector<std::string>& lines) {
    SkillEffect out;
    for (const auto& line : lines) {
        std::string low;
        for (const char c : line) {
            low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        out.attack_bonus = out.attack_bonus || low.find("added to attack bonus") != std::string::npos;
        out.attack_damage =
            out.attack_damage || low.find("added to attack damage") != std::string::npos;
        out.armor_class = out.armor_class || low.find("added to armor class") != std::string::npos;
        out.hit_points = out.hit_points || low.find("adds to hit points") != std::string::npos;
        out.spell_points = out.spell_points || low.find("adds to spell points") != std::string::npos;
        out.shop_prices =
            out.shop_prices || low.find("adjusts shop prices") != std::string::npos;
    }
    return out;
}

// Raising a skill from `points` costs the next number of skill points: the
// second point costs 2, the third 3. The tables never state the price; this
// staircase is the engine's own. `inferred`
[[nodiscard]] inline int raise_cost(int points) noexcept { return points + 1; }

// The one weapon skill a new character starts with, one point, by class.
// The class table says which weapons a class may use, in prose; which single
// skill begins at one point is this engine's reading of it. `inferred`
[[nodiscard]] inline std::string_view starting_skill(std::string_view class_name) noexcept {
    if (class_name == "Knight") {
        return "Sword";
    }
    if (class_name == "Paladin" || class_name == "Cleric") {
        return "Mace";
    }
    if (class_name == "Archer") {
        return "Bow";
    }
    return "Staff";  // Druid, Sorcerer
}

// The skill a spell answers to: its school's own name, which is also that
// skill's SKILLDES.TXT heading.
[[nodiscard]] inline std::string_view school_skill(data::SpellSchool school) noexcept {
    return data::school_name(school);
}

// How a merchant's points move a price: one percent per point in the
// party's favor, to at most half. The table says only "in your favor"; the
// rate and the floor are the engine's own. `inferred`
[[nodiscard]] inline int haggled_price(int asking, int merchant_points) noexcept {
    const int percent = merchant_points > 50 ? 50 : merchant_points;
    const int off = asking * percent / 100;
    const int paid = asking - off;
    return paid < 1 ? 1 : paid;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SKILLS_HPP
