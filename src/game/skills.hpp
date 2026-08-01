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

#include <array>
#include <cctype>
#include <cstdint>
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

// **Retracted: the rank is not a point threshold.** Expert at four points and
// master at seven stood here, invented because no table states where the
// ranks begin. No table states it because the ranks are not in the points at
// all: every skill is **one packed byte**, the points in its low six bits and
// the rank in the top two, and a teacher sets the bits. The training routine
// at `0x42d0d8` masks the points with `0x3f` and leaves `0xc0` untouched when
// it raises them, which is exactly a number and a rank sharing a byte; the
// armour-recovery routine tests `0x40` at `0x481c1e` and `0x80` at
// `0x481c84` and lifts the penalty by half and then wholly, which is
// `SKILLDES.TXT`'s "Recovery penalty reduced" and "eliminated" lines with the
// bits behind them. `observed`
//
// So a character with thirty points and no bits set is still a novice, and
// one with two points and `0x80` set is a master. That is a different game
// from the one this engine was playing.

inline constexpr int kSkillPointMask = 0x3f;
inline constexpr int kSkillRankShift = 6;

[[nodiscard]] inline constexpr int skill_points(int packed) noexcept {
    return packed & kSkillPointMask;
}

// 0 novice, 1 expert, 2 master. **`0xc0` never occurs**: the teacher at
// `0x4969f3` masks the byte with `0x3f` first and then ORs exactly one bit —
//
//     mov  cl, byte [edx + eax + 0x60]
//     and  cl, 0x3f            ; the rank bits go first
//     mov  byte [eax], cl
//     ...
//     neg  ecx                 ; ecx = 0 for expert, 1 for master
//     sbb  cl, cl              ; 0x00 or 0xff
//     and  cl, 0x40            ; 0x00 or 0x40
//     add  cl, 0x40            ; 0x40 or 0x80
//     or   dl, cl
//
// so the pair is one of three states and never both. `observed`
[[nodiscard]] inline constexpr int skill_rank(int packed) noexcept {
    return (packed >> kSkillRankShift) & 0x3;
}

inline constexpr std::array<std::string_view, 3> kRankNames{"Normal", "Expert", "Master"};

// What a teacher charges: **2000 gold for expert, 5000 for master**, built by
// the same three instructions that choose the bit — `and eax, 0xbb8` on a
// mask of the rank flag, then `add eax, 0x7d0`. `observed` at 0x496cdf.
inline constexpr int kExpertPrice = 2000;
inline constexpr int kMasterPrice = kExpertPrice + 3000;

[[nodiscard]] inline constexpr int teach_price(int rank) noexcept {
    return rank >= 2 ? kMasterPrice : kExpertPrice;
}

// What a teacher does: clear the rank bits, then set exactly one. The rank is
// clamped at master because the byte has no fourth state.
[[nodiscard]] inline constexpr int teach_rank(int packed, int rank) noexcept {
    const int held = rank < 0 ? 0 : rank > 2 ? 2 : rank;
    return skill_points(packed) | (held << kSkillRankShift);
}

[[nodiscard]] inline int rank_of(int packed) noexcept { return skill_rank(packed); }

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
// `packed` is the skill byte as the character record holds it: the points in
// the low six bits, the rank in the top two. A byte of zero is a skill the
// character does not have.
[[nodiscard]] inline SkillPower skill_power(const std::vector<std::string>& lines, int packed) {
    SkillPower out;
    const int points = skill_points(packed);
    if (points <= 0) {
        return out;
    }
    const int rank = rank_of(packed);
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

// **Retracted.** One invented weapon skill per class stood here, read off the
// class prose. The table at `0x4c2694` says what a class actually begins
// with, and it is **two** skills rather than one — see `class_starting_skills`
// below. Two of the six guesses were wrong besides: a Paladin begins with
// Sword, not Mace, and a Sorcerer with Dagger, not Staff.

// The skill a spell answers to: its school's own name, which is also that
// skill's SKILLDES.TXT heading.
[[nodiscard]] inline std::string_view school_skill(data::SpellSchool school) noexcept {
    return data::school_name(school);
}

// The thirty-one slots, named.
//
// `SKILLDES.TXT` ships **exactly thirty-one rows**, and the array at `+0x60`
// is exactly thirty-one bytes, so slot `n` is row `n`. The names below are
// that file's, in its own order. `observed`
//
// **Corrected.** A twenty-four-name list stood here with Light and Dark at
// 16 and 17, ahead of Spirit, Mind and Body. That ordering was wrong, and
// the class table below is what caught it: read the Cleric's row on the old
// order and the class starts with Spirit rather than Body and may choose
// Light at creation; read the Sorcerer's and no sorcerer may ever learn Dark.
// Read on `SKILLDES.TXT`'s order both rows come out exactly as the game
// plays. The first twelve names are unchanged, so nothing that indexed the
// weapon groups moves.
inline constexpr std::array<std::string_view, 31> kSkillNames{
    "Staff",        "Sword",      "Dagger",     "Axe",        "Spear",
    "Bow",          "Mace",       "Blaster",    "Shield",     "Leather",
    "Chain",        "Plate",      "Fire",       "Air",        "Water",
    "Earth",        "Spirit",     "Mind",       "Body",       "Light",
    "Dark",         "Identify",   "Merchant",   "Repair",     "Bodybuilding",
    "Meditation",   "Perception", "Diplomacy",  "Thievery",   "Disarm Traps",
    "Learning"};

[[nodiscard]] inline int skill_id(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kSkillNames.size(); ++i) {
        if (kSkillNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

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

// ---------------------------------------------------------------------------
// Where a character's own skills live: the byte array at `+0x60`.
//
// Two hundred bytes sat unread between the resistances and the item records.
// **Thirty-one of them are the skills, one byte each, indexed by skill id.**
// `observed` at 0x484899, where the party-validity check walks indices 0
// through 0x1e inclusive.
//
// The byte is packed. The training routine at 0x42d0d8 masks it with
// **0x3f** to get the points, so the low six bits are the number and the top
// two are the mastery rung -- Novice, Expert, Master, Grandmaster, which is
// exactly four values. `observed` for the mask and the two spare bits;
// `inferred` that the pair is the mastery, from there being four of them and
// from the rank getter reading the same byte.
//
// Zero means the skill is not learned at all: every reader tests the whole
// byte against zero before doing anything with it (0x484173, 0x484899).
// `observed`

inline constexpr int kSkillArrayOffset = 0x60;
inline constexpr int kSkillSlots = 31;

// The training routine's own three rules, read straight out of 0x42d0d8:
//
//   * raising a skill from `n` costs **`n + 1`** points, and the character is
//     refused if the pool holds less;
//   * the points stop at **60** -- `cmp dl, 0x3c; jae` refuses at or above
//     it, so 59 is the last rung that may be bought and 60 the ceiling;
//   * the pool is a dword at **`+0x1410`**, four bytes before the hit points,
//     and 0x42d10e writes back what the cost left of it.
//
// `observed`, all three.
inline constexpr int kSkillPointCap = 60;
inline constexpr int kFreeSkillPointsOffset = 0x1410;

// `skill_points` and `skill_rank` are defined above, beside the retraction
// that made them necessary.
[[nodiscard]] inline constexpr int skill_mastery(int packed) noexcept {
    return skill_rank(packed);
}

[[nodiscard]] inline constexpr int skill_raise_cost(int points) noexcept { return points + 1; }

// Spend from the pool to buy one point, on the routine's own terms. Returns
// false and touches nothing when the skill is unlearned, already at the
// ceiling, or the pool is short.
[[nodiscard]] inline constexpr bool train_skill(int& packed, int& pool) noexcept {
    if (packed == 0) {
        return false;
    }
    const int points = skill_points(packed);
    const int cost = skill_raise_cost(points);
    if (pool < cost || points >= kSkillPointCap) {
        return false;
    }
    packed += 1;  // the mastery bits ride along untouched, as `inc al` leaves them
    pool -= cost;
    return true;
}

// ---------------------------------------------------------------------------
// Which class may hold which skill: a **six by thirty-one byte table at
// `0x4c2694`**, stride thirty-one, indexed by the class family and the slot.
// The family is the class divided by three (`0x484181`), which is why there
// are six rows for eighteen classes and why the six hit-point bases have six
// entries too. The table runs right up to the weapon-recovery table at
// `0x4c2750`, which is where its length is fixed from. `observed`
//
// The trainer's list at `0x49c864` and `0x49ca81` tests the byte for
// **non-zero** and nothing finer, then skips any skill the character already
// holds — so **zero means the class may never learn it**, and that much is
// `observed`.
//
// What separates 1, 2 and 3 is `inferred`, from three things agreeing.
// `0x484150`'s first body walks the slots whose byte is **1** and hands back
// the first or second of them; every family has exactly two, and in all six
// cases they are the pair that class is known to begin play with — Knight
// sword and leather, Cleric mace and body, Sorcerer dagger and fire, Paladin
// sword and spirit, Archer bow and air, Druid staff and earth. Its other two
// bodies walk the slots whose byte is **2**, which is the list a new
// character chooses from. So: **1 granted, 2 offered at creation, 3 learnable
// only later, 0 never**.
inline constexpr int kSkillFamilies = 6;
inline constexpr int kClassesPerFamily = 3;

enum class SkillAccess : int {
    Never = 0,
    Granted = 1,
    Offered = 2,
    Later = 3,
};

inline constexpr std::array<std::array<std::uint8_t, kSkillSlots>, kSkillFamilies>
    kClassSkillTable{{
        // Knight
        {3, 1, 2, 2, 2, 2, 3, 3, 2, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 2, 0, 2, 3, 0,
         2, 3},
        // Cleric
        {2, 0, 0, 0, 0, 3, 1, 3, 2, 2, 3, 0, 0, 0, 0, 0, 2, 2, 1, 3, 3, 2, 3, 2, 3, 2, 3, 2, 0,
         3, 3},
        // Sorcerer
        {2, 0, 1, 0, 0, 3, 0, 3, 0, 2, 0, 0, 1, 2, 2, 2, 0, 0, 0, 3, 3, 2, 3, 2, 3, 2, 3, 2, 0,
         3, 3},
        // Paladin
        {3, 1, 2, 3, 2, 3, 2, 3, 2, 2, 2, 3, 0, 0, 0, 0, 1, 3, 3, 0, 0, 3, 3, 3, 3, 3, 2, 2, 0,
         2, 3},
        // Archer
        {3, 2, 2, 2, 3, 1, 3, 3, 0, 2, 3, 0, 2, 1, 3, 3, 0, 0, 0, 0, 0, 2, 3, 3, 3, 3, 2, 2, 0,
         2, 3},
        // Druid
        {1, 0, 3, 0, 0, 3, 2, 3, 3, 2, 0, 0, 3, 3, 2, 1, 2, 3, 2, 0, 0, 2, 3, 2, 3, 2, 3, 3, 0,
         3, 2},
    }};

[[nodiscard]] inline constexpr int class_family(int class_id) noexcept {
    const int family = class_id / kClassesPerFamily;
    return family < 0 ? 0 : family >= kSkillFamilies ? kSkillFamilies - 1 : family;
}

[[nodiscard]] inline constexpr SkillAccess class_skill_access(int class_id, int slot) noexcept {
    if (slot < 0 || slot >= kSkillSlots) {
        return SkillAccess::Never;
    }
    return static_cast<SkillAccess>(
        kClassSkillTable[static_cast<std::size_t>(class_family(class_id))]
                        [static_cast<std::size_t>(slot)]);
}

// What the trainer's list tests, and nothing finer.
[[nodiscard]] inline constexpr bool class_may_learn(int class_id, int slot) noexcept {
    return class_skill_access(class_id, slot) != SkillAccess::Never;
}

// The two a class begins with, in slot order.
[[nodiscard]] inline std::array<int, 2> class_starting_skills(int class_id) {
    std::array<int, 2> found{-1, -1};
    std::size_t at = 0;
    for (int slot = 0; slot < kSkillSlots && at < found.size(); ++slot) {
        if (class_skill_access(class_id, slot) == SkillAccess::Granted) {
            found[at++] = slot;
        }
    }
    return found;
}

// What a made party must have before the game will start: the check at
// 0x484890 walks all four characters at stride 0x161c and demands **at least
// four** non-zero skills in each. `observed`
inline constexpr int kStartingSkillsRequired = 4;

template <typename SkillOf>
[[nodiscard]] inline bool character_skills_chosen(SkillOf&& skill_of) {
    int learned = 0;
    for (int slot = 0; slot < kSkillSlots; ++slot) {
        if (skill_of(slot) != 0) {
            ++learned;
        }
    }
    return learned >= kStartingSkillsRequired;
}


}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SKILLS_HPP
