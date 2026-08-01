#ifndef STARHAVEN_GAME_COMBAT_HPP
#define STARHAVEN_GAME_COMBAT_HPP

// Hitting things, and being hit.
//
// The monsters' side of this is all in `MONSTERS.TXT`: hit points, armour
// class, two attacks with a type and damage dice, a chance to use each, a
// recovery time, five resistances and the experience for killing it. All 212
// of the shipped damage cells parse, and so do all 78 weapons' — see
// docs/formats/text-tables.md.
//
// The party's side is not in any table. What a character hits for, how often,
// and how hard the monsters find them, are this engine's, and are marked
// where they are defined.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/data/dice.hpp"
#include "core/data/item_generation.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/spell_effects.hpp"
#include "core/data/treasure.hpp"
#include "core/random.hpp"
#include "core/world/map_session.hpp"
#include "game/inventory.hpp"
#include "game/conditions.hpp"
#include "game/party.hpp"
#include "game/skills.hpp"
#include "game/fire_dark.hpp"
#include "game/weapon_specials.hpp"

namespace starhaven::game {

// **How the game measures a distance.** State 1-and-3 differences two pairs,
// takes both absolute values, and answers `max + min / 2` — the octagonal
// approximation, no square root anywhere:
//
//     sub ecx, edx ; cdq ; xor ecx, edx ; sub ecx, edx   ; abs
//     cmp ecx, eax
//     jle ...
//     sar eax, 1 ; add eax, ecx                          ; max + min/2
//
// `observed` at 0x403b99. Every threshold the AI tests — the awareness cut at
// 5120, the second at 1024 — is against this, not against a Euclidean length,
// and the two differ by up to twelve percent on a diagonal.
[[nodiscard]] inline constexpr int octagonal_distance(int dx, int dz) noexcept {
    const int a = dx < 0 ? -dx : dx;
    const int b = dz < 0 ? -dz : dz;
    return a >= b ? a + b / 2 : b + a / 2;
}

// The award's two constants, out of `0x421520`.
inline constexpr int kAwardBasePercent = 9;
inline constexpr std::int64_t kExperienceCeiling = 0xee6b2800LL;

// The wielder's own points in the skill the held weapon answers to, or none
// when the hand is empty, the weapon broken, or its group not one the skill
// list names. `ITEMS.TXT`'s group headings are `SKILLDES.TXT`'s own, which is
// what lets the two be joined by name at all.
[[nodiscard]] inline int wielded_skill_points(const Character& who,
                                              const data::ItemStatsTable& items) noexcept {
    const auto slot = static_cast<std::size_t>(Slot::Weapon);
    const int held = who.equipped[slot];
    if (held <= 0 || who.equipped_broken[slot]) {
        return 0;
    }
    const auto* row = items.at(static_cast<std::size_t>(held));
    if (row == nullptr || row->skill_group.empty()) {
        return 0;
    }
    const auto it = who.skills.find(row->skill_group);
    return it == who.skills.end() ? 0 : skill_points(it->second);
}

// How near a monster has to be to swing at the party, in world units.
// `inferred`
inline constexpr float kMeleeRange = 400.0f;

// And how near the party has to be to swing back. The same, so that whoever
// closes the distance can be answered.
inline constexpr float kPartyReach = kMeleeRange;

// How far a missile carries, both ways: a monster whose attack column names
// one ("Arrow", "Fire", "Elec"...) shoots from here, and a bow answers over
// the same ground. The table names the missiles, not their range; this
// distance is the engine's own. `inferred`
inline constexpr float kMissileRange = 2048.0f;

// How far a monster notices anything at all. The AI's decision routine cuts
// out before considering anything else when the distance reaches **5120**,
// and uses **1024** as its second threshold within that. `observed` at
// 0x4021a3 and 0x402317; see docs/formats/event-actors.md.
inline constexpr float kAwarenessRange = 5120.0f;
inline constexpr float kCloseRange = 1024.0f;

// Whether any of a monster's two attacks is a missile: the Miss column's
// "0" means none.
[[nodiscard]] inline bool has_missile(const data::MonsterStatsEntry& monster) noexcept {
    for (const auto& attack : monster.attacks) {
        if (!attack.missile.empty() && attack.missile != "0") {
            return true;
        }
    }
    return false;
}

// The Miss column's kind, for the caller that flies a sprite for it.
[[nodiscard]] inline std::string_view missile_kind(
    const data::MonsterStatsEntry& monster) noexcept {
    for (const auto& attack : monster.attacks) {
        if (!attack.missile.empty() && attack.missile != "0") {
            return attack.missile;
        }
    }
    return {};
}

// A character with no weapon. `inferred`
inline constexpr int kBareHandSides = 3;

// How long between a character's blows, in seconds. `inferred`
inline constexpr float kPartyRecovery = 1.0f;

// The world clock's own unit, traced. The time-advance routine at `0x4880a0`
// adds the frame's elapsed to the 64-bit counter at `0x908d08` and makes a
// calendar of it by multiplying by 30/128 (the float at `0x4b9374`) before
// dividing by 60, 60, 24 and 7 — so a unit is 30/128 of a world second. A
// real second is 128 of them: the sound code at `0x488d79` turns a table of
// plain seconds into units by multiplying by 128.0. The world therefore runs
// at thirty times real time. `observed`
inline constexpr float kClockUnitsPerSecond = 128.0f;

// One point of the `Rec` column, in real seconds. The counter at `+0x137c`
// is set from a queued amount times 32/15 (the double at `0x4b9318`) and
// drained by the frame's own clock units inside that same time-advance
// routine. 128 units a second over 32/15 units a point is exactly **sixty
// points a second**. `observed` See docs/formats/player-record.md.
inline constexpr float kRecoveryScale = 1.0f / 60.0f;

// The same figure under its old name, kept for the monster table's column —
// and it is the same counter, not an analogy. A monster's recovery is the
// dword at `+0x6c` of its 548-byte runtime record, filled by the very
// message handler that fills a character's `+0x137c` (kind 3 with an actor
// index where kind 4 takes a party slot) and scaled by the same 32/15, then
// counted down and clamped at zero at `0x401b5d`. Its elapsed — the global
// at `0x4d51c4` — is the same field of the same timer class as the world
// clock's own `0x4d519c`, both filled from `GetTickCount()` by the shared
// method at `0x420ec0`, so the two carry one unit. `observed` throughout.
// See docs/formats/player-record.md.
inline constexpr float kMonsterRecoveryScale = kRecoveryScale;

// What a `Rec` value costs in seconds, at the traced rate.
[[nodiscard]] inline constexpr float recovery_seconds(int rec) noexcept {
    return static_cast<float>(rec) * kRecoveryScale;
}

// The special bonus that drains recovery half again as fast: row 17 of the
// game's own special table, **"of Recovery"**. The drain walks the sixteen
// equipment anchors at `+0x1428`, skips whatever the flag byte at `+0x13c`
// marks broken, and takes 50% when a worn item's special id at `+0xc` is
// this. `observed` at `0x488605`..`0x488635`.
inline constexpr int kSpecialOfRecovery = 17;
inline constexpr float kOfRecoveryDrain = 1.5f;

// How long a monster shows its flinch before standing again, in seconds. The
// frame table gives each group a length, but not in any unit this engine has
// pinned down, so this is a plain duration. `inferred`
inline constexpr float kWinceSeconds = 0.4f;

// What a monster is doing in the fight, one per session actor.
struct Combatant {
    int hit_points = 0;
    int max_hit_points = 0;
    float recovery = 0.0f;  // seconds until it can strike again
    float wince = 0.0f;     // seconds left of flinching
    bool alive = true;

    // What the party's spells laid on it, in seconds of the fight's clock,
    // each by its spell's own words: fear flees and breaks on damage, slow
    // doubles the recovery, paralysis holds still and cannot retaliate,
    // charm calms until hurt.
    float feared = 0.0f;
    float slowed = 0.0f;
    float paralyzed = 0.0f;
    float charmed = 0.0f;

    // **The disposition, on the original's own shape.** `0x421c50` does not
    // read a state: it rolls `rand() % 100` against the byte at `+0x4d` and
    // answers **2**, else the same against `+0x47` and answers **1**, else
    // **0** — every tick, for every actor in range. So an afflicted monster
    // wavers rather than being wholly one thing while a timer runs.
    // `observed` for the two bytes and the order they are rolled in.
    //
    // These two carry them. That an affliction sets its byte to a hundred
    // while its own timer runs is this engine's, since the spells' words give
    // durations and not percentages. `inferred`
    int second_percent = 0;  // +0x4d, rolled first, answers 2
    int first_percent = 0;   // +0x47, rolled second, answers 1
    // What the last roll answered, kept so a caller that moves the actors can
    // act on it: 0, 1 or 2.
    int disposition = 0;
    bool aware = false;  // within the awareness cut of 5120 this tick
};

// The conditions a spell can lay on a monster.
enum class MonsterCondition : std::uint8_t { Fear, Slow, Paralyze, Charm };

// The five resistance columns, by the name the attack types use. `observed`
[[nodiscard]] inline int resistance_index(std::string_view type) noexcept {
    if (type == "Fire") {
        return static_cast<int>(data::Resistance::Fire);
    }
    if (type == "Elec") {
        return static_cast<int>(data::Resistance::Electricity);
    }
    if (type == "Cold") {
        return static_cast<int>(data::Resistance::Cold);
    }
    if (type == "Pois") {
        return static_cast<int>(data::Resistance::Poison);
    }
    if (type == "Magic") {
        return static_cast<int>(data::Resistance::Magic);
    }
    return -1;
}

// Which resistance an attack type is answered by. The monster table writes
// "Phys", "Fire", "Elec", "Cold", "Pois", "Magic" and "Ener"; the first is
// resisted by armour rather than by a resistance, and "Ener" has no column of
// its own so it goes unresisted. `observed` for the names, `inferred` for what
// answers "Ener".
[[nodiscard]] inline int resistance_to(const data::MonsterStatsEntry& monster,
                                       std::string_view type) noexcept {
    const int which = resistance_index(type);
    return which < 0 ? 0 : monster.resistances[static_cast<std::size_t>(which)];
}

// And the same question of a character, with whatever a potion laid on top.
[[nodiscard]] inline int resistance_to(const Character& who, std::string_view type) noexcept {
    const int which = resistance_index(type);
    return which < 0 ? 0
                     : who.resistances[static_cast<std::size_t>(which)] +
                           who.temp_resistances[static_cast<std::size_t>(which)] +
                           who.gear_resistances[static_cast<std::size_t>(which)];
}

// Whether a blow lands, traced to the original's own routine at
// `0x421cb0`: it rolls `rand() % (armour + 2 * attack + 30)` — armour the
// target's class, attack the striker's bonus — and compares the roll,
// plus a caller-supplied modifier, against a threshold the blow's kind
// picks:
//
//   kind 2:  (armour + 15) + (armour + 15) / 2   — the steepest bar
//   kind 3:  2 * armour + 30                      — steeper still with armour
//   any other kind (4 among them): armour + 15    — the plain bar
//
// Landing needs roll plus the modifier to reach the bar. An unarmoured
// target on the plain bar is hit about half the time however sharp the
// striker; armour raises kind 2's and kind 3's bars faster than it widens
// the span, so skill is what buys hits against armour. `observed` for the
// arithmetic; which caller passes which kind is read from the call sites
// (spell and missile paths pass 2, 3 and 4), and mapping the party's own
// swing onto the plain bar is `inferred`.
enum class BlowKind : std::uint8_t { Steep = 2, Shot = 3, Plain = 4 };

[[nodiscard]] inline bool blow_lands(int armour, int attack, BlowKind kind, int modifier,
                                     Mm6Random& random) noexcept {
    const int span = armour + 2 * attack + 30;
    const int roll = static_cast<int>(random.next() % static_cast<unsigned>(span < 1 ? 1 : span));
    const int plain = armour + 15;
    const int threshold = kind == BlowKind::Steep  ? plain + plain / 2
                          : kind == BlowKind::Shot ? 2 * armour + 30
                                                   : plain;
    return roll + modifier >= threshold;
}

// What a resistance does to a blow, traced to the original's own routine
// at `0x421dc0`: the resistance is **not** a percentage taken off. The
// routine reads the target's element byte (the six sit at +0x50..+0x55
// of the record, in the table's own order), answers **immune at 200 or
// more** — which is what the table's "Imm" cells compile to — and
// otherwise rolls up to four times: each roll is `rand() % (resistance +
// 30)`, and while the roll lands **30 or above** the damage is halved
// again. So resistance buys repeated halvings by chance, four at most,
// and a resistance of zero still halves on most rolls. `observed`
// **Whose resistance.** `0x421dc0` takes an **actor**, not a character. Two
// of its four callers test the actor's `+0x114`/`+0x118` pair in the same
// breath as the call (`0x430f36`, `0x431915`) and a third reaches its record
// as `esi - 0xa0` from the AI's own state pointer. So the six bytes at
// `+0x50`..`+0x55`, with 200 meaning immune, are the **monster's**
// resistances, and the element order rotated by two is that jump table's,
// not a character's.
//
// A character's five resistances are **words at `+0x1254`..`+0x1267`, base
// and modifier in pairs**, sitting immediately below the buff array at
// `+0x1268` — the property setter's ten case bodies write them and the
// getter's five read them. `Character::resistances` and
// `Character::gear_resistances` are already that pair. `observed`
[[nodiscard]] inline int after_resistance(int damage, int resistance, Mm6Random& random) noexcept {
    // The table's "Imm" cells parse to kResistanceImmune (-1); the
    // original's own immunity is its byte reaching 200, the same thing
    // said two ways.
    if (resistance == data::kResistanceImmune || resistance >= 200) {
        return 0;
    }
    const auto span = static_cast<unsigned>((resistance < 0 ? 0 : resistance) + 30);
    for (int tries = 0; tries < 4; ++tries) {
        if (static_cast<int>(random.next() % span) < 30) {
            break;
        }
        damage /= 2;
    }
    return damage;
}

// The same, where no dice are at hand: the expected outcome of the four
// rolls, kept for the tools and tests that want a number rather than a
// throw. `inferred` from the traced rule above.
[[nodiscard]] inline int after_resistance(int damage, int resistance) noexcept {
    if (resistance == data::kResistanceImmune || resistance >= 200) {
        return 0;
    }
    const int span = (resistance < 0 ? 0 : resistance) + 30;
    // The chance a single roll lands 30 or above, in percent.
    const int halve_percent = span <= 30 ? 0 : (span - 30) * 100 / span;
    int expected = damage * 100;
    for (int tries = 0; tries < 4; ++tries) {
        expected = expected - expected * halve_percent / 200;
    }
    return expected / 100;
}

// The dice a character swings for: whatever is in their weapon slot, or a
// fist. Before there were slots this took the first weapon in the pack, which
// meant picking something up could change what you were fighting with.
[[nodiscard]] inline data::Dice weapon_of(const Character& who, const data::ItemStatsTable& items) {
    const int held = who.equipped[static_cast<std::size_t>(Slot::Weapon)];
    if (held > 0 && !who.equipped_broken[static_cast<std::size_t>(Slot::Weapon)]) {
        if (const auto* row = items.at(static_cast<std::size_t>(held)); row != nullptr) {
            if (const data::Dice& dice = row->modifier_1_dice; !dice.empty()) {
                return dice;
            }
        }
    }
    return {1, kBareHandSides, 0};
}

// And what armour takes off the chance of being hit: the flat modifier each
// worn piece carries. `inferred`
[[nodiscard]] inline int armour_of(const Character& who, const data::ItemStatsTable& items) {
    int total = 0;
    for (std::size_t slot = 0; slot < who.equipped.size(); ++slot) {
        const int id = who.equipped[slot];
        if (id <= 0 || who.equipped_broken[slot]) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(id));
        if (row == nullptr || !row->modifier_1_dice.empty()) {
            continue;  // a weapon's modifier is dice, not armour
        }
        total += data::parse_int(row->modifier_1, 0);
    }
    return total;
}

// "No actor", for a search that found nothing.
inline constexpr std::size_t kNoActor = static_cast<std::size_t>(-1);

// One fight: the party on one side, whatever is on the map on the other.
class Battle {
public:
    // Take the session's actors as they stand. Every one starts at the hit
    // points its row gives.
    void reset(const world::MapSession& session, const data::MonsterStatsTable& monsters,
               std::uint32_t seed) {
        random_ = Mm6Random{seed};
        experience_ = 0;
        gold_ = 0;
        loot_.clear();
        artifacts_ = {};
        combatants_.clear();
        combatants_.reserve(session.actors.size());
        for (const auto& actor : session.actors) {
            Combatant c;
            const auto id = static_cast<std::size_t>(actor.monster_id);
            if (actor.monster_id > 0 && id <= monsters.entries().size()) {
                c.max_hit_points = monsters.entries()[id - 1].hit_points;
            }
            c.hit_points = c.max_hit_points;
            c.alive = c.hit_points > 0;
            combatants_.push_back(c);
        }
    }

    // Take in whatever was summoned after the fight began: new actors join
    // at their row's full health, and everyone already fighting keeps their
    // wounds.
    void recruit(const world::MapSession& session, const data::MonsterStatsTable& monsters) {
        for (std::size_t i = combatants_.size(); i < session.actors.size(); ++i) {
            Combatant c;
            const auto id = static_cast<std::size_t>(session.actors[i].monster_id);
            if (session.actors[i].monster_id > 0 && id <= monsters.entries().size()) {
                c.max_hit_points = monsters.entries()[id - 1].hit_points;
            }
            c.hit_points = c.max_hit_points;
            c.alive = c.hit_points > 0;
            combatants_.push_back(c);
        }
    }

    [[nodiscard]] bool alive(std::size_t actor) const noexcept {
        return actor < combatants_.size() && combatants_[actor].alive;
    }
    [[nodiscard]] std::size_t size() const noexcept { return combatants_.size(); }

    // The aimed one's blood, for the bar the interface draws: current and
    // full, zero when out of range.
    [[nodiscard]] std::pair<int, int> health_of(std::size_t actor) const noexcept {
        if (actor >= combatants_.size()) {
            return {0, 0};
        }
        return {combatants_[actor].hit_points, combatants_[actor].max_hit_points};
    }

    // Which of its eight animations a monster should be drawn in. The dead
    // stay dead: a corpse keeps its death picture rather than disappearing.
    [[nodiscard]] world::MonsterAnimation animation_of(std::size_t actor) const noexcept {
        if (actor >= combatants_.size()) {
            return world::MonsterAnimation::Stand;
        }
        if (!combatants_[actor].alive) {
            return world::MonsterAnimation::Death;
        }
        return combatants_[actor].wince > 0.0f ? world::MonsterAnimation::Wince
                                               : world::MonsterAnimation::Stand;
    }

    // Put every fallen monster back on its feet at full health, which is what
    // a refill means on a map whose monsters are placed rather than spawned.
    // Maps with spawn points roll new groups instead — see
    // world::respawn_monsters — and reset this battle whole. `inferred`
    void refill() {
        for (auto& c : combatants_) {
            c.hit_points = c.max_hit_points;
            c.alive = c.max_hit_points > 0;
            c.wince = 0.0f;
            c.recovery = 0.0f;
        }
    }

    // Whether anything on the map is still standing near a point.
    // Fell an actor without loot or experience: how a map's memory
    // restores its dead on return.
    void kill(std::size_t actor) noexcept {
        if (actor < combatants_.size()) {
            combatants_[actor].alive = false;
            combatants_[actor].hit_points = 0;
        }
    }

    // Whether any monster still stands, for the arena's judge.
    [[nodiscard]] bool anything_alive() const noexcept {
        for (const auto& c : combatants_) {
            if (c.alive) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool anything_near(const world::MapSession& session, const render::Vec3& at,
                                     float range) const noexcept {
        if (combatants_.size() != session.actors.size()) {
            return false;
        }
        for (std::size_t i = 0; i < combatants_.size(); ++i) {
            if (!combatants_[i].alive) {
                continue;
            }
            const auto& p = session.actors[i].position;
            const float dx = p.x - at.x;
            const float dz = p.z - at.z;
            if (dx * dx + dz * dz <= range * range) {
                return true;
            }
        }
        return false;
    }

    // Experience earned and not yet handed out.
    [[nodiscard]] int unclaimed_experience() const noexcept { return experience_; }

    // What a monster last decided, for a caller that moves it: its awareness
    // and the three-way answer of `0x421c50`.
    [[nodiscard]] bool aware_of_party(std::size_t actor) const noexcept {
        return actor < combatants_.size() && combatants_[actor].aware;
    }
    [[nodiscard]] int disposition_of_actor(std::size_t actor) const noexcept {
        return actor < combatants_.size() ? combatants_[actor].disposition : 0;
    }

    // Lay a condition on a monster for so many seconds of fight time. False
    // when there is nothing there to afflict.
    bool afflict(std::size_t actor, MonsterCondition condition, float seconds) {
        if (actor >= combatants_.size() || !combatants_[actor].alive) {
            return false;
        }
        Combatant& c = combatants_[actor];
        switch (condition) {
        case MonsterCondition::Fear:
            c.feared = std::max(c.feared, seconds);
            break;
        case MonsterCondition::Slow:
            c.slowed = std::max(c.slowed, seconds);
            break;
        case MonsterCondition::Paralyze:
            c.paralyzed = std::max(c.paralyzed, seconds);
            break;
        case MonsterCondition::Charm:
            c.charmed = std::max(c.charmed, seconds);
            break;
        }
        return true;
    }

    // Whether a monster may move at all: a paralyzed one is held where it
    // stands, in its spell's own words.
    [[nodiscard]] bool can_move(std::size_t actor) const noexcept {
        return actor >= combatants_.size() || combatants_[actor].paralyzed <= 0.0f;
    }

    // The noises the fight made since last asked: a session actor and which
    // slot of its `DSOUNDS.BIN` set to play — 0 attack, 1 die. The caller
    // drains and plays them; the fight only remembers.
    struct Noise {
        std::size_t actor = 0;
        int action = 0;
    };
    [[nodiscard]] std::vector<Noise> take_noises() { return std::exchange(noises_, {}); }

    // The shots fired since last asked: which actor loosed one, for the
    // caller to fly its Miss column's kind at the party.
    [[nodiscard]] std::vector<std::size_t> take_shots() { return std::exchange(shots_, {}); }

    // The monster rows killed since last asked, for the bounty board.
    [[nodiscard]] std::vector<int> take_kills() { return std::exchange(kills_, {}); }

    // The spells cast since last asked: who, and which Spells.txt id — for
    // the caller to fly the school's bolt and play the spell's own sound.
    [[nodiscard]] std::vector<std::pair<std::size_t, int>> take_casts() {
        return std::exchange(casts_, {});
    }

    // And the gold, which is the party's rather than any one character's.
    [[nodiscard]] int unclaimed_gold() const noexcept { return gold_; }
    int take_gold() noexcept {
        const int taken = gold_;
        gold_ = 0;
        return taken;
    }

    // What pickpockets have cut from the purse and not yet been charged.
    int take_stolen() noexcept {
        const int taken = stolen_;
        stolen_ = 0;
        return taken;
    }

    // The items kills have left and nobody has picked up yet.
    [[nodiscard]] const std::vector<data::GeneratedItem>& unclaimed_loot() const noexcept {
        return loot_;
    }
    std::vector<data::GeneratedItem> take_loot() {
        std::vector<data::GeneratedItem> taken = std::move(loot_);
        loot_.clear();
        return taken;
    }

    // **The nine, and the ceiling.** `lea ecx, [ecx + ebx + 9]` adds a flat
    // nine to the Learning and hireling terms before the share is scaled by
    // them, so a character with neither still collects nine percent over the
    // plain share. And `0x421631` clamps the 64-bit total at `0xee6b2800` —
    // four billion — exactly as the script property adder does. `observed`

    // Share it among whoever is still standing, the way a party splits a kill.
    // Nobody standing means nobody collects, and it waits. `inferred`
    //
    // **The original's own shape, at last.** Four searches said no instruction
    // awarded experience for a kill; all four were wrong, and the award is
    // `0x421520`. It counts the characters whose condition slots 13..17 are
    // clear, divides the experience among them, and adds each one's Learning
    // and any hired Teacher's percent on top. That is what this does.
    // `bonus_percent` is a hired teacher's cut on top, from their row's prose.
    void award(std::array<Character, 4>& party, int bonus_percent = 0) {
        if (experience_ <= 0) {
            return;
        }
        int standing = 0;
        for (const auto& who : party) {
            standing += who.hit_points > 0 ? 1 : 0;
        }
        if (standing == 0) {
            return;
        }
        // The routine's own order: divide first, then add a percentage on
        // top of the share. `0x421585` is the division and `0x42161a` the
        // 64-bit add with the four-billion clamp.
        const int share = experience_ / standing;
        for (auto& who : party) {
            if (who.hit_points <= 0) {
                continue;
            }
            // "Skill increases amount of experience received", read per
            // character because that is where the skill lives.
            int learned = 0;
            if (const auto it = who.skills.find("Learning"); it != who.skills.end()) {
                learned = learning_percent(it->second);
            }
            const int over = share * (learned + bonus_percent + kAwardBasePercent) / 100;
            const std::int64_t total = static_cast<std::int64_t>(who.experience) + share + over;
            who.experience = static_cast<int>(std::min<std::int64_t>(total, kExperienceCeiling));
        }
        experience_ = 0;
    }

    // The party strikes one monster. Returns what happened, for the message
    // line, or empty when the blow was not possible at all.
    // `skill` is what the striker's weapon skill grants at its rank, read
    // from that skill's own SKILLDES.TXT lines by the caller, who holds the
    // table: the attack bonus, any damage, the Dagger's triple chance, the
    // Mace's stun, the Bow's second arrow.
    std::string strike(std::size_t actor, Character& who, const Pack& pack,  // NOLINT
                       const world::MapSession& session, const data::MonsterStatsTable& monsters,
                       const data::ItemStatsTable& items,
                       const data::RandomItemTable& random_items,
                       const data::StandardBonusTable& standard_bonuses,
                       const data::SpecialBonusTable& special_bonuses,
                       const SkillPower& skill = {}, const data::Dice& rider = {},
                       std::string_view rider_element = {}, int hired_weapon_points = 0) {
        if (!alive(actor) || who.hit_points <= 0) {
            return {};
        }
        const auto id = static_cast<std::size_t>(session.actors[actor].monster_id);
        if (id == 0 || id > monsters.entries().size()) {
            return {};
        }
        const auto& monster = monsters.entries()[id - 1];

        // Whether it lands, by the original's own rule — and with the
        // original's own bonus: reading the to-hit's callers settled that
        // its second argument is the one struck (its armour is read at
        // `+0x60`) and the first is the striker, so the getter really is
        // the attacker's bonus. It reads the attribute **raw** and scales
        // the total by the worst condition the character carries — the walk
        // once read here as a skill search is a condition search. `observed`
        // The getter mixes one attribute's bonuses with another's stored
        // value and runs the sum through the sheet's own ladder: what the
        // gear gives Accuracy, plus Speed cut by the years and by whatever
        // ails, then banded. See src/game/skills.hpp.
        const int aged_speed = ailing_attribute(who, Attribute::Speed, kAgeAttackPercent);
        int attack = attribute_bonus(traced_attack_bonus(
            who.gear_attributes[static_cast<std::size_t>(Attribute::Accuracy)], aged_speed));
        // "Skill added to Attack Bonus" — every one of the nine weapon rows
        // in `SKILLDES.TXT` opens with that line, and until now nothing read
        // it. The wielder's own points in the weapon's skill group go on top
        // of the ladder, one for one, which is what the line says and no more.
        // `observed` in the rows; that a point is worth exactly one is this
        // engine's reading, as it has been since the line was first parsed.
        // "Two point bonus to all weapon skills for all characters" — an Arms
        // Master's, a Weapons Master's, a Squire's. Parsed for a long while
        // and applied nowhere until the weapon skill itself reached the roll.
        attack += wielded_skill_points(who, items) + hired_weapon_points;
        // The stored term the attack-bonus getter adds beside the gear's and
        // the ladder's — `+0x1570`, the first of the four.
        attack += who.derived_bonus[static_cast<std::size_t>(DerivedBonus::AttackBonus)];
        // A drawn bow is the shot's own kind — `2 × armour + 30`, which is
        // why archery against armour asks for skill; a swing is the plain
        // bar. Which call site passes which kind is the original's;
        // reading the party's bow as kind 3 is `inferred`.
        const int held = who.equipped[static_cast<std::size_t>(Slot::Weapon)];
        const auto* held_row = held > 0 ? items.at(static_cast<std::size_t>(held)) : nullptr;
        const bool shooting = held_row != nullptr &&
                              held_row->equip_type == data::ItemEquipType::Missile;
        if (!blow_lands(monster.armor_class, attack,
                        shooting ? BlowKind::Shot : BlowKind::Plain, 0, random_)) {
            return who.name + " misses " + monster.name;
        }

        // A weapon does physical damage, which no resistance column answers;
        // the call is here so an elemental one would be answered correctly.
        int damage = data::roll(weapon_of(who, items), random_) +
                     attribute_bonus(who.attribute(Attribute::Might)) + skill.damage +
                     who.derived_bonus[static_cast<std::size_t>(
                         shooting ? DerivedBonus::ShootDamage : DerivedBonus::AttackDamage)];
        // Each of these appends: a blow may be two-handed *and* tripled *and*
        // carry a special's name. They used to overwrite one another, so only
        // the last one to fire was ever spoken.
        std::string flourish;
        // A blade in the left hand — the Shield slot, opened by its skill's
        // own "in left hand" line — swings its own dice alongside.
        if (const int off = who.equipped[static_cast<std::size_t>(Slot::Shield)];
            off > 0 && !who.equipped_broken[static_cast<std::size_t>(Slot::Shield)]) {
            if (const auto* row = items.at(static_cast<std::size_t>(off)); row != nullptr) {
                if (const data::Dice& dice = row->modifier_1_dice; !dice.empty()) {
                    damage += data::roll(dice, random_);
                    flourish += ", both hands";
                }
            }
        }
        // "Bow fires two arrows on every attack": a second roll of the
        // same weapon joins the blow.
        if (skill.second_arrow) {
            damage += data::roll(weapon_of(who, items), random_);
            flourish += ", twice-feathered";
        }
        // "Chance to cause triple damage equal to skill."
        if (skill.triple_percent > 0 &&
            static_cast<int>(random_.next() % 100) < skill.triple_percent) {
            damage *= 3;
            flourish += ", a vicious strike";
        }
        damage = after_resistance(damage < 1 ? 1 : damage, resistance_to(monster, "Phys"));
        damage = damage < 1 ? 1 : damage;
        // A weapon's elemental rider — "Adds 6-8 points of Cold damage" —
        // rolls apart and is answered by its own element.
        if (!rider.empty()) {
            damage += after_resistance(data::roll(rider, random_),
                                       resistance_to(monster, rider_element));
        }

        // Every worn special the executable's own table answers for: each
        // rolls its band, is met by the element's own resistance column, and
        // joins the blow. Two artifacts it names by id add a flat amount,
        // and the vampiric pair take a fifth of the blow back as health.
        // See src/game/weapon_specials.hpp.
        int drained = 0;
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            const int worn = who.equipped[slot];
            if (worn <= 0 || who.equipped_broken[slot]) {
                continue;
            }
            const int special = who.worn_special[slot];
            if (const auto* extra = special_rider(special); extra != nullptr) {
                const int rolled =
                    extra->low + static_cast<int>(random_.next() %
                                                  static_cast<std::uint64_t>(
                                                      extra->high - extra->low + 1));
                damage += after_resistance(
                    rolled, resistance_to(monster, element_column(extra->element)));
                flourish += ", " + std::string(extra->name);
            }
            if (const int flat = artifact_extra(worn); flat > 0) {
                damage += flat;
            }
            if (special_drains(special, worn)) {
                drained = 1;
            }
        }
        if (drained > 0) {
            drained = vampiric_gain(damage);
        }

        // "Chance to stun equal to skill": the stunned lose their next
        // moment — a second on their recovery is the engine's own length.
        if (skill.stun_percent > 0 && alive(actor) &&
            static_cast<int>(random_.next() % 100) < skill.stun_percent) {
            combatants_[actor].recovery += 1.0f;
            flourish += ", stunned";
        }
        if (drained > 0) {
            who.hit_points = std::min(who.max_hit_points, who.hit_points + drained);
            flourish += ", drinking deep";
        }
        std::string what = land(actor, damage, monster, who.name, items, random_items,
                                standard_bonuses, special_bonuses);
        return flourish.empty() ? what : what + flourish;
    }

    // A spell's blow at one monster: the prose's flat part, plus one roll of
    // the scaling part per point of skill, answered by the resistance of the
    // spell's own element.
    // A spell that reaches past its target: everything alive within the
    // burst takes the same roll, answered by its own resistance. The
    // radius is **measured**: Ring of Fire's case sets 512 at normal rank
    // and 1024 above it, which is the only blast the executable puts a
    // number on, and it is taken as the shape of all of them. "In sight"
    // is read as the whole map, which is what the words say. `observed`
    // for the 512; `inferred` that other blasts share it.
    static constexpr float kBlastRadius = static_cast<float>(kRingOfFireRadius[0]);

    std::string smite_area(std::size_t actor, const data::SpellRange& flat,
                           const data::SpellRange& per_skill, int skill,
                           std::string_view element, const std::string& caster,
                           data::SpellReach reach, const world::MapSession& session,
                           const data::MonsterStatsTable& monsters,
                           const data::ItemStatsTable& items,
                           const data::RandomItemTable& random_items,
                           const data::StandardBonusTable& standard_bonuses,
                           const data::SpecialBonusTable& special_bonuses) {
        std::string last = smite(actor, flat, per_skill, skill, element, caster, session,
                                 monsters, items, random_items, standard_bonuses,
                                 special_bonuses);
        if (reach == data::SpellReach::Single || actor >= session.actors.size()) {
            return last;
        }
        const render::Vec3 centre = session.actors[actor].position;
        std::size_t caught = 0;
        for (std::size_t i = 0; i < session.actors.size(); ++i) {
            if (i == actor || !alive(i)) {
                continue;
            }
            if (reach == data::SpellReach::Blast) {
                const render::Vec3& at = session.actors[i].position;
                const float dx = at.x - centre.x;
                const float dy = at.y - centre.y;
                const float dz = at.z - centre.z;
                if (dx * dx + dy * dy + dz * dz > kBlastRadius * kBlastRadius) {
                    continue;
                }
            }
            if (!smite(i, flat, per_skill, skill, element, caster, session, monsters, items,
                       random_items, standard_bonuses, special_bonuses)
                     .empty()) {
                ++caught;
            }
        }
        if (caught > 0) {
            last += "  (" + std::to_string(caught) + " more caught)";
        }
        return last;
    }

    std::string smite(std::size_t actor, const data::SpellRange& flat,
                      const data::SpellRange& per_skill, int skill, std::string_view element,
                      const std::string& caster, const world::MapSession& session,
                      const data::MonsterStatsTable& monsters, const data::ItemStatsTable& items,
                      const data::RandomItemTable& random_items,
                      const data::StandardBonusTable& standard_bonuses,
                      const data::SpecialBonusTable& special_bonuses) {
        if (!alive(actor)) {
            return {};
        }
        const auto id = static_cast<std::size_t>(session.actors[actor].monster_id);
        if (id == 0 || id > monsters.entries().size()) {
            return {};
        }
        const auto& monster = monsters.entries()[id - 1];
        int damage = roll_range(flat);
        for (int i = 0; i < skill; ++i) {
            damage += roll_range(per_skill);
        }
        damage = after_resistance(damage < 1 ? 1 : damage, resistance_to(monster, element));
        return land(actor, damage < 1 ? 1 : damage, monster, caster, items, random_items,
                    standard_bonuses, special_bonuses);
    }

    // The monsters take their turn. Anything alive and in reach swings at a
    // character who is still standing.
    std::string update(float dt, const world::MapSession& session,
                       const data::MonsterStatsTable& monsters,
                       const data::SpellStatsTable& spells, std::array<Character, 4>& party,
                       const render::Vec3& eye, std::int64_t now = 0) {
        std::string last;
        if (combatants_.size() != session.actors.size()) {
            return last;
        }
        for (std::size_t i = 0; i < combatants_.size(); ++i) {
            Combatant& c = combatants_[i];
            if (!c.alive) {
                continue;
            }
            c.wince = c.wince > dt ? c.wince - dt : 0.0f;
            const auto tick = [dt](float& left) {
                left = left > dt ? left - dt : 0.0f;
            };
            tick(c.feared);
            tick(c.slowed);
            tick(c.paralyzed);
            tick(c.charmed);
            // Each affliction holds its byte at a hundred while it runs.
            c.second_percent = c.charmed > 0.0f ? 100 : 0;
            c.first_percent = c.feared > 0.0f ? 100 : 0;

            const auto& actor = session.actors[i];
            const float dx = actor.position.x - eye.x;
            const float dz = actor.position.z - eye.z;
            const float range2 = dx * dx + dz * dz;
            // **The awareness cut comes first.** The decision routine tests
            // the distance against 5120 before it considers anything else,
            // and idles outright beyond it. `observed`
            // The awareness cut is measured the game's way, not Euclidean.
            c.aware = octagonal_distance(static_cast<int>(dx), static_cast<int>(dz)) <=
                      static_cast<int>(kAwarenessRange);
            if (!c.aware) {
                c.disposition = 0;
                continue;
            }
            c.recovery -= dt;
            if (c.recovery > 0.0f) {
                continue;
            }
            // Then the disposition is rolled, in the original's own order.
            c.disposition = disposition_of(c);
            if (c.disposition != 0 || c.paralyzed > 0.0f) {
                continue;
            }
            const auto id = static_cast<std::size_t>(actor.monster_id);
            if (id == 0 || id > monsters.entries().size()) {
                continue;
            }
            const auto& monster = monsters.entries()[id - 1];
            // Past arm's reach, only a monster whose Miss column names a
            // missile attacks — and its shot is reported for the caller to
            // fly.
            // Past arm's reach the missile band serves whoever can use
            // it: a Miss-column shooter, or a caster with a spell to throw.
            const bool in_melee = range2 <= kMeleeRange * kMeleeRange;
            if (!in_melee && (range2 > kMissileRange * kMissileRange ||
                              (!has_missile(monster) && monster.spell_percent == 0))) {
                continue;
            }
            // Slow "doubles the recovery rate of a single monster".
            c.recovery = recovery_seconds(monster.recovery) * (c.slowed > 0.0f ? 2.0f : 1.0f);

            cast_id_ = 0;
            if (std::string what = swing(monster, spells, party, now); !what.empty()) {
                noises_.push_back({i, 0});
                if (cast_id_ > 0) {
                    casts_.push_back({i, cast_id_});
                } else if (!in_melee) {
                    shots_.push_back(i);
                }
                last = std::move(what);
            }
        }
        return last;
    }

private:
    // `0x421c50`'s three-way answer, rolled fresh every tick.
    int disposition_of(const Combatant& c) {
        if (c.second_percent > 0 &&
            static_cast<int>(random_.next() % 100) < c.second_percent) {
            return 2;
        }
        if (c.first_percent > 0 && static_cast<int>(random_.next() % 100) < c.first_percent) {
            return 1;
        }
        return 0;
    }

    // One amount from a prose range, inclusive.
    int roll_range(const data::SpellRange& range) noexcept {
        if (range.empty()) {
            return 0;
        }
        return range.low +
               static_cast<int>(random_.next() %
                                static_cast<unsigned>(range.high - range.low + 1));
    }

    // A landed blow, whoever struck it: the wound, the flinch, and on a kill
    // the experience and the treasure code's payout.
    std::string land(std::size_t actor, int damage, const data::MonsterStatsEntry& monster,
                     const std::string& attacker, const data::ItemStatsTable& items,
                     const data::RandomItemTable& random_items,
                     const data::StandardBonusTable& standard_bonuses,
                     const data::SpecialBonusTable& special_bonuses) {
        Combatant& target = combatants_[actor];
        target.hit_points -= damage;
        target.wince = kWinceSeconds;
        // "If a creature takes damage ... the spell will be broken", and a
        // charmed one "will immediately become hostile again".
        target.feared = 0.0f;
        target.charmed = 0.0f;
        std::string what = attacker + " hits " + monster.name + " for " + std::to_string(damage);
        if (target.hit_points <= 0) {
            target.alive = false;
            target.hit_points = 0;
            target.wince = 0.0f;
            noises_.push_back({actor, 1});
            kills_.push_back(monster.id);
            experience_ += monster.experience;
            // And whatever its treasure code leaves behind: one roll against
            // the chance, then the gold and the item the code names.
            if (const data::Treasure drop = data::parse_treasure(monster.treasure);
                !drop.empty() && static_cast<int>(random_.next() % 100) < drop.chance) {
                if (!drop.gold.empty()) {
                    gold_ += data::roll(drop.gold, random_);
                }
                data::GeneratedItem rolled;
                if (drop.item_level > 0 &&
                    data::generate_random_item(random_items, items, standard_bonuses,
                                               special_bonuses,
                                               static_cast<std::size_t>(drop.item_level),
                                               data::treasure_item_type(drop.item_kind), random_,
                                               artifacts_,
                                               rolled) == data::ItemGenerationError::None &&
                    rolled.item_id > 0) {  // a kind nothing matches rolls the blank row
                    loot_.push_back(rolled);
                }
            }
            what += " and kills it";
        }
        return what;
    }

    // One monster's blow at whoever in the party is not yet dead — the
    // unconscious can still be hit, and a blow that lands on one kills them.
    // `inferred`
    std::string swing(const data::MonsterStatsEntry& monster, const data::SpellStatsTable& spells,  // NOLINT
                      std::array<Character, 4>& party, std::int64_t now = 0) {
        std::vector<std::size_t> standing;
        for (std::size_t i = 0; i < party.size(); ++i) {
            if (!party[i].dead()) {
                standing.push_back(i);
            }
        }
        if (standing.empty()) {
            return {};
        }
        // The Pref column names who this monster goes for: class initials —
        // "D,S" on the Terrible Eye, the casters — with M and F read as a
        // gender taste. `inferred` for those two letters; the digit values
        // (2, 3, 4 on a handful of rows) match nothing tested and are
        // ignored. A preferred victim is chosen when one stands; otherwise
        // anyone.
        std::vector<std::size_t> preferred;
        if (!monster.preference.empty() && monster.preference != "0") {
            for (const std::size_t i : standing) {
                for (const char c : monster.preference) {
                    const char initial =
                        party[i].class_name.empty() ? '?' : party[i].class_name.front();
                    const bool female = face_is_female(party[i].face);
                    if ((c == initial) || (c == 'F' && female) || (c == 'M' && !female)) {
                        preferred.push_back(i);
                        break;
                    }
                }
            }
        }
        const auto& pool = preferred.empty() ? standing : preferred;
        Character& target = party[pool[random_.next() % pool.size()]];
        const bool was_down = target.hit_points <= 0;

        // The table's own spell first. Note that the row gives the spell no
        // percentage of its own — `Spl,Mas,Skil` is two bytes at `+0x22` and
        // `+0x23`, the mastery and the skill — so the `Use%` column serves
        // both the second attack and the cast. `observed` for the two bytes;
        // `inferred` that one column does double duty, for want of another.
        //
        // Cast as often as the row's percent says, at its written mastery and
        // skill — the number the prose's
        // per-skill dice scale by — and answered by the target's resistance
        // to the spell's own element. A spell whose prose carries no number
        // is not cast; the monster falls back to its blows.
        if (monster.spell_percent > 0 &&
            static_cast<int>(random_.next() % 100) < monster.spell_percent) {
            const data::MonsterSpell chosen = data::parse_monster_spell(monster.spells);
            if (const auto* spell = data::find_spell_tolerant(spells, chosen.name);
                spell != nullptr && !chosen.empty()) {
                const data::SpellEffect effect = data::parse_spell_effect(*spell, chosen.mastery);
                if (!effect.damage.empty() || !effect.damage_per_skill.empty()) {
                    int damage = roll_range(effect.damage);
                    for (int i = 0; i < chosen.skill; ++i) {
                        damage += roll_range(effect.damage_per_skill);
                    }
                    damage = after_resistance(damage < 1 ? 1 : damage,
                                              resistance_to(target, spell->element));
                    target.hit_points -= damage;
                    cast_id_ = spell->id;
                    std::string what = monster.name + " casts " + spell->name + " at " +
                                       target.name + " for " + std::to_string(damage);
                    what += fell(target, was_down, damage);
                    return what;
                }
            }
        }

        // **Corrected by the runtime row.** This used to take `Att%` as the
        // second attack's chance, marked `inferred` from where its values sat.
        // The row settles it: the two attacks are **parallel blocks six bytes
        // apart** — `Type` at `+0x16` and `+0x1c`, the damage pair at
        // `+0x17`/`+0x18` and `+0x1d`/`+0x1e`, `Miss` at `+0x1a` and `+0x20`,
        // and the percentage at `+0x1b` and `+0x21`. So `Att%` closes the
        // first block and `Use%` the second, and each attack carries its own
        // chance. `observed`
        const data::Dice second = data::parse_dice(monster.attacks[1].damage);
        const bool use_second =
            !second.empty() && monster.attacks[1].chance > 0 &&
            static_cast<int>(random_.next() % 100) < monster.attacks[1].chance;
        for (const auto& attack :
             {use_second ? monster.attacks[1] : monster.attacks[0], monster.attacks[0]}) {
            const data::Dice dice = data::parse_dice(attack.damage);
            if (dice.empty()) {
                continue;
            }
            // And the same rule the other way: the monster's own level
            // stands in for its attack bonus, which the table does not
            // state, and an attack its Miss column names flies as the
            // shot's own kind. `inferred` for both readings.
            const bool flies = !attack.missile.empty() && attack.missile != "0";
            if (!blow_lands(target.armor_class, monster.level,
                            flies ? BlowKind::Shot : BlowKind::Plain, 0, random_)) {
                return monster.name + " misses " + target.name;
            }
            int damage =
                after_resistance(data::roll(dice, random_), resistance_to(target, attack.type));
            // "Halves damage from incoming ranged attacks (such as rocks and
            // arrows)" — Shield, out of the target's own slot 3. Its row
            // gives the halving and no number, so there is nothing to scale.
            if (flies && target.buffs.power(CharacterBuff::Shield, now) > 0) {
                damage = damage / 2;
            }
            target.hit_points -= damage;
            std::string what =
                monster.name + " hits " + target.name + " for " + std::to_string(damage);
            // The row's own on-hit word: the trigger is the table's, the
            // magnitudes and the one-in-five chance are this engine's.
            // `inferred`
            if (damage > 0 && !monster.bonus.empty() && random_.next() % 5 == 0) {
                const std::string_view bonus = monster.bonus;
                int level = 1;   // the digit inside Poison2, Disease3
                int repeat = 1;  // the xN suffix, as a multiplier
                for (std::size_t i = 0; i < bonus.size(); ++i) {
                    if (bonus[i] >= '1' && bonus[i] <= '9') {
                        if (i > 0 && (bonus[i - 1] == 'x' || bonus[i - 1] == 'X')) {
                            repeat = bonus[i] - '0';
                        } else {
                            level = bonus[i] - '0';
                        }
                    }
                }
                if (bonus.substr(0, 6) == "Poison" || bonus.substr(0, 4) == "Pois") {
                    if (level > target.poisoned) {
                        target.poisoned = level;
                        target.poisoned_minute = now;
                        what += ", poisoning them";
                    }
                } else if (bonus.substr(0, 7) == "Disease") {
                    if (level > target.diseased) {
                        target.diseased = level;
                        target.diseased_minute = now;
                        what += ", infecting them";
                    }
                } else if (bonus == "Uncon") {
                    // Knocked to zero; `fell` below says what that means.
                    target.hit_points = 0;
                } else if (bonus.substr(0, 7) == "DrainSP") {
                    const int drained = std::min(target.spell_points, monster.level * repeat);
                    if (drained > 0) {
                        target.spell_points -= drained;
                        what += ", draining " + std::to_string(drained) + " spell points";
                    }
                } else if (bonus.substr(0, 5) == "Steal") {
                    stolen_ += monster.level * 5 * repeat;
                    what += ", cutting your purse";
                } else if (bonus.substr(0, 3) == "Age") {
                    target.age += repeat;
                    what += ", aging them";
                } else if (bonus.substr(0, 7) == "BrkItem" || bonus.substr(0, 6) == "BrkArm" ||
                           bonus.substr(0, 9) == "Brkweapon") {
                    const Slot slot = bonus.substr(0, 9) == "Brkweapon" ? Slot::Weapon
                                      : bonus.substr(0, 6) == "BrkArm"  ? Slot::Armor
                                                                        : static_cast<Slot>(
                                                                            random_.next() %
                                                                            kSlotCount);
                    const auto at = static_cast<std::size_t>(slot);
                    if (target.equipped[at] > 0 && !target.equipped_broken[at]) {
                        target.equipped_broken[at] = true;
                        what += ", breaking their " + std::string(slot_name(slot));
                    }
                } else if (bonus == "Dead" || bonus.substr(0, 5) == "Errad") {
                    // The two words that end a character outright; a temple
                    // is the only way back.
                    target.affliction = bonus == "Dead" ? "Dead" : "Eradicated";
                    target.affliction_minute = now;
                    target.hit_points = std::min(target.hit_points, 0);
                    what += bonus == "Dead" ? ", killing them" : ", eradicating them";
                } else if (bonus == "Asleep" || bonus == "Affraid" || bonus == "Weak" ||
                           bonus == "Drunk" || bonus == "Insane" || bonus == "Paralyze" ||
                           bonus.substr(0, 5) == "Stone" || bonus.substr(0, 5) == "Curse") {
                    if (target.affliction.empty()) {
                        target.affliction = std::string(bonus.substr(0, bonus.find('x')));
                        target.affliction_minute = now;
                        what += ", leaving them " + target.affliction;
                    }
                }
            }
            what += fell(target, was_down, damage);
            return what;
        }
        return {};
    }

    // What becomes of whoever a blow just landed on: a sleeper is struck
    // awake, a standing character at zero drops, and one already down is
    // killed. `inferred`
    static std::string fell(Character& target, bool was_down, int damage) {
        std::string what;
        if (damage > 0 && target.affliction == "Asleep") {
            target.affliction.clear();
            what += ", waking them";
        }
        if (target.hit_points <= 0) {
            target.hit_points = 0;
            if (was_down && damage > 0) {
                target.affliction = "Dead";
                what += " and kills them";
            } else if (!was_down) {
                what += " and drops them";
            }
        }
        return what;
    }

    std::vector<Combatant> combatants_;
    std::vector<Noise> noises_;
    std::vector<std::size_t> shots_;
    std::vector<std::pair<std::size_t, int>> casts_;
    std::vector<int> kills_;
    int cast_id_ = 0;  // the spell the current swing cast, for update()
    int experience_ = 0;
    int gold_ = 0;
    std::vector<data::GeneratedItem> loot_;
    int stolen_ = 0;
    data::ArtifactGenerationState artifacts_;
    Mm6Random random_{1};
};

// Which living monster the party is aiming at and can reach, or kNoActor.
// A monster is aimed at when the look ray passes through its body — the
// DMONLIST record's own radius and height, so a dragon is hard to miss and
// a bat hard to hit. The nearest such body wins, the way a ray does; the
// slack on the cylinder and the fallback body are the engine's. `inferred`
[[nodiscard]] inline std::size_t aimed_actor(const world::MapSession& session, const Battle& battle,
                                             const render::Vec3& eye, const render::Vec3& forward,
                                             float reach) {
    constexpr float kSlack = 12.0f;
    std::size_t best = kNoActor;
    float best_distance = 0.0f;
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        if (!battle.alive(i)) {
            continue;
        }
        const render::Vec3& at = session.actors[i].position;
        const render::Vec3 d{at.x - eye.x, at.y - eye.y, at.z - eye.z};
        const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (distance > reach || distance <= 0.001f) {
            continue;
        }
        float radius = 48.0f;
        float height = 160.0f;
        const auto id = static_cast<std::size_t>(session.actors[i].monster_id);
        if (const auto* body = id > 0 ? session.monsters.at(id - 1) : nullptr;
            body != nullptr && body->radius > 0) {
            radius = static_cast<float>(body->radius);
            height = static_cast<float>(body->height);
        }
        // The ray's closest approach, split into floor plan and height.
        const float along = d.x * forward.x + d.y * forward.y + d.z * forward.z;
        if (along <= 0.0f) {
            continue;
        }
        const render::Vec3 p{eye.x + forward.x * along, eye.y + forward.y * along,
                             eye.z + forward.z * along};
        const float flat = std::sqrt((p.x - at.x) * (p.x - at.x) + (p.z - at.z) * (p.z - at.z));
        const bool tall = p.y >= at.y - kSlack && p.y <= at.y + height + kSlack;
        if (flat > radius + kSlack || !tall) {
            continue;
        }
        if (best == kNoActor || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_COMBAT_HPP
