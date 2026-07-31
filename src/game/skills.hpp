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
    // "Permits use of dagger in left hand" (expert), "...sword in left
    // hand" (master): the off hand opens at the line's own rank.
    bool left_hand = false;
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
        out.left_hand = out.left_hand || low.find("in left hand") != std::string::npos;
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

// The executable's own skill numbering, and the two tables the attack
// bonus is built from. `GLOBAL.TXT` names the skills in a run from row
// 271, so id 0 is Staff and 23 Repair Item; the percentage table at
// `0x4c27fc` gives each id its weight, and the priority list at
// `0x4c276c` is the order the getter searches — it takes the **first
// skill the character actually has** and scales that one. `observed`
// See docs/formats/player-record.md.
inline constexpr std::array<std::string_view, 24> kSkillNames{
    "Staff",      "Sword",      "Dagger",     "Axe",        "Spear",     "Bow",
    "Mace",       "Blaster",    "Shield",     "Leather",    "Chain",     "Plate",
    "Fire",       "Air",        "Water",      "Earth",      "Light",     "Dark",
    "Spirit",     "Mind",       "Body",       "Identify",   "Merchant",  "Repair"};

// **Retracted.** A per-skill percentage table and a fourteen-entry skill
// priority list stood here, read from `0x4c27fc` and `0x4c276c`. They are
// neither. The walk that uses them reads a **64-bit value at
// `+0x1380 + 8 × id` and tests it for non-zero** — a condition timestamp,
// not a count of skill points — and the order read as condition ids is
// exactly "worst first". The percentages are per-condition multipliers and
// live in src/game/conditions.hpp.

// The attack bonus the original assembles: an attribute read raw, plus the
// first skill in the priority order the character holds, weighted by that
// skill's own percentage. `observed` for the walk and the tables; which
// attribute stat 4 names is this engine's reading of Accuracy. `inferred`
// How long a strike costs, in `Rec` points, by the gear's own skill. The
// routine at `0x481a80` opens with the word at `0x4c2750` as its default —
// **100, bare-handed** — and then replaces it with the entry the equipped
// weapon's skill group names, indexing this fourteen-word table at
// `0x4c2750` by the skill id plus one. `observed`
inline constexpr std::array<int, 14> kRecoveryBySkill{100, 100, 90,  60, 100, 80, 100,
                                                     80,  30,  10, 10,  20, 30,  0};

// With nothing in hand.
inline constexpr int kBareHandRecovery = kRecoveryBySkill[0];

// What a piece of gear's skill group costs, or nothing when the group is
// not one of the twelve the table covers.
//
// `ITEMS.TXT` ships **thirteen** groups, not twelve: beside the twelve the
// skill list names it gives three weapons the group **"Club"**, which is no
// skill at all. The routine's own shape answers for it — it loads entry 0,
// the bare-hand 100, and only overwrites when the item's skill byte names a
// row — so an unknown group leaves the default standing. Callers must do the
// same and not read a zero as "instant". `observed` for the shape,
// `observed` for the thirteenth group in the table.
[[nodiscard]] inline int gear_recovery(std::string_view skill_group) noexcept {
    for (std::size_t i = 0; i < 12; ++i) {
        if (kSkillNames[i] == skill_group) {
            return kRecoveryBySkill[i + 1];
        }
    }
    return 0;
}

// Worn armour and a held shield add their own entry on top of the weapon's,
// and the wearer's skill takes it back: the routine halves it when the
// packed skill byte has bit `0x40` and drops it entirely on bit `0x80` —
// which are SKILLDES.TXT's "Recovery penalty reduced" and "eliminated"
// lines, now with a number behind them. `observed` at 0x481c1e and 0x481c84.
[[nodiscard]] inline constexpr int worn_recovery_penalty(int base, int lift) noexcept {
    return lift >= 2 ? 0 : lift == 1 ? base / 2 : base;
}

// The three weapon skills whose expert line "gains a quicker attack": the
// routine tests the item's skill byte against 2, 4 and 6 — **Sword, Axe and
// Bow** — and, when the wearer's packed skill byte carries either of the two
// rank bits, subtracts that skill's level from the recovery outright. The
// table's own prose says the same in words: "expert swordsmen gain a quicker
// attack", "expert axe fighters gain a little more speed", "expert archers
// gain a speed increase". `observed` at 0x481dfc..0x481e1e.
[[nodiscard]] inline bool skill_quickens_attack(std::string_view group) noexcept {
    return group == "Sword" || group == "Axe" || group == "Bow";
}

// A worn item "of Swiftness" — the special table's row 59 — takes a flat
// **20** off the recovery, as do the two artifacts the routine names by id,
// Merlin (404) and Percival (405). `observed` at 0x481e52..0x481e71.
inline constexpr int kSpecialOfSwiftness = 59;
inline constexpr int kSwiftItemRelief = 20;
inline constexpr std::array<int, 2> kSwiftArtifacts{404, 405};

// The attack bonus, read to the getter's return at `0x47e403`. Its shape:
//
//   ladder( stat4's spell bonus + stat4's gear bonus
//           + stored[+0x28] × age% × condition% + stored[+0x2a] )
//   + stat15's award, spell and gear contributions
//   + the byte at `+0x1570`
//
// where `ladder` is the sheet's own attribute curve. `observed` at
// `0x47e354`..`0x47e3fd`.
//
// **It is lopsided, and that is the finding.** The getter asks the two bonus
// getters for stat **4** but reads the *stored* pair at `+0x28`/`+0x2a`,
// which by the two anchors that fix the stored run — max hit points asks id
// 3 and reads `+0x20`, max spell points asks id 2 and reads `+0x1c` — is
// stat **5**'s. So it mixes one attribute's bonuses with another's stored
// value. `observed` for the mixture; that the two are Accuracy and Speed is
// `inferred` from `stats.txt`'s order.
[[nodiscard]] inline int traced_attack_bonus(int accuracy_bonus, int aged_ailed_speed) noexcept {
    return accuracy_bonus + aged_ailed_speed;
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

// Haggling and the two counter services. These were weighted by a table
// that turned out to be about conditions, not skills; the weights are gone,
// and what is left is the plain reading of each skill's own SKILLDES.TXT
// line — one percent a point, floored at half price, never below a gold.
// Every number here is this engine's. `inferred`
[[nodiscard]] inline int haggled_price(int asking, int merchant_points) noexcept {
    const int percent = merchant_points > 50 ? 50 : merchant_points;
    const int paid = asking - asking * percent / 100;
    return paid < 1 ? 1 : paid;
}

[[nodiscard]] inline int weighted_identify(int points) noexcept { return points; }

[[nodiscard]] inline int weighted_repair(int points) noexcept { return points; }


}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SKILLS_HPP
