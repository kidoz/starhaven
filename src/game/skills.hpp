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

// The rank thresholds. The tables state what expert and master ranks do,
// but not where they begin — the original keeps that with teachers no table
// lists — so the steps are this engine's own: expert at four points, master
// at seven. `inferred`
inline constexpr int kExpertAt = 4;
inline constexpr int kMasterAt = 7;

[[nodiscard]] inline int rank_of(int points) noexcept {
    return points >= kMasterAt ? 2 : points >= kExpertAt ? 1 : 0;
}

// Everything a skill grants at a rank, each from the table's own line for
// that rank; a line beyond the held rank grants nothing yet.
struct SkillPower {
    int to_hit = 0;          // "Skill added to Attack Bonus", times any doubling
    int damage = 0;          // "Skill added to Attack Damage"
    int armor = 0;           // "Skill added to Armor Class", times any doubling
    int stun_percent = 0;    // "Chance to stun equal to skill"
    int triple_percent = 0;  // "Chance to cause triple damage equal to skill"
    bool second_arrow = false;  // "Bow fires two arrows on every attack"
    // "Skill reduces recovery time": how much per point is unnamed, so one
    // percent per point to at most half is the engine's own. `inferred`
    float recovery_scale = 1.0f;
    int price_percent = 0;  // "Skill adjusts shop prices...", times any doubling
    int hp_bonus = 0;       // "Skill adds to Hit Points", times any doubling
    int sp_bonus = 0;       // "Skill adds to Spell Points", the same way
    // The armor skills' higher lines: 0 the full penalty, 1 "Recovery
    // penalty reduced", 2 "Recovery penalty eliminated".
    int armor_penalty_lift = 0;
};

// Read what `points` in a skill grant at their rank, from the skill's own
// SKILLDES.TXT lines in rank order.
[[nodiscard]] inline SkillPower skill_power(const std::vector<std::string>& lines, int points) {
    SkillPower out;
    if (points <= 0) {
        return out;
    }
    const int rank = rank_of(points);
    int multiplier = 1;
    bool base_attack = false, base_armor = false, base_prices = false, adds_damage = false;
    bool cuts_recovery = false, adds_hp = false, adds_sp = false;
    for (int i = 0; i <= rank && i < static_cast<int>(lines.size()); ++i) {
        std::string low;
        for (const char c : lines[static_cast<std::size_t>(i)]) {
            low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (low.find("triple effect") != std::string::npos) {
            multiplier = 3;
        } else if (low.find("double effect") != std::string::npos) {
            multiplier = multiplier < 2 ? 2 : multiplier;
        }
        base_attack = base_attack || low.find("added to attack bonus") != std::string::npos;
        adds_damage = adds_damage || low.find("added to attack damage") != std::string::npos;
        base_armor = base_armor || low.find("added to armor class") != std::string::npos;
        base_prices = base_prices || low.find("adjusts shop prices") != std::string::npos;
        out.stun_percent = low.find("chance to stun equal to skill") != std::string::npos
                               ? points
                               : out.stun_percent;
        out.triple_percent =
            low.find("triple damage equal to skill") != std::string::npos ? points
                                                                          : out.triple_percent;
        out.second_arrow = out.second_arrow || low.find("fires two arrows") != std::string::npos;
        cuts_recovery = cuts_recovery || low.find("reduces recovery time") != std::string::npos;
        adds_hp = adds_hp || low.find("adds to hit points") != std::string::npos;
        adds_sp = adds_sp || low.find("adds to spell points") != std::string::npos;
        if (low.find("recovery penalty eliminated") != std::string::npos) {
            out.armor_penalty_lift = 2;
        } else if (low.find("recovery penalty reduced") != std::string::npos) {
            out.armor_penalty_lift = out.armor_penalty_lift < 1 ? 1 : out.armor_penalty_lift;
        }
    }
    out.to_hit = base_attack ? points * multiplier : 0;
    out.damage = adds_damage ? points : 0;
    out.armor = base_armor ? points * multiplier : 0;
    out.price_percent = base_prices ? points * multiplier : 0;
    out.hp_bonus = adds_hp ? points * multiplier : 0;
    out.sp_bonus = adds_sp ? points * multiplier : 0;
    if (cuts_recovery) {
        const int percent = points > 50 ? 50 : points;
        out.recovery_scale = 1.0f - static_cast<float>(percent) / 100.0f;
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

// What wearing armor costs the swing before skill lifts it: a tenth slower
// in leather, a fifth in chain, three tenths in plate. The table names the
// penalty and who lifts it; these sizes are the engine's own. `inferred`
[[nodiscard]] inline float armor_penalty(std::string_view skill_group) noexcept {
    if (skill_group == "Leather") {
        return 0.10f;
    }
    if (skill_group == "Chain") {
        return 0.20f;
    }
    if (skill_group == "Plate") {
        return 0.30f;
    }
    return 0.0f;
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
