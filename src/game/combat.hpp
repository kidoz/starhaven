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
#include "game/party.hpp"
#include "game/skills.hpp"

namespace starhaven::game {

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

// The monster table's recovery column is a bare number, 0 to 200-odd, with no
// unit. Treating it as hundredths of a second puts the slowest monsters at
// about two seconds between blows. `inferred`
inline constexpr float kMonsterRecoveryScale = 0.01f;

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

// What a resistance does to a blow: none of it gets through if the target is
// immune, and otherwise the percentage is taken off. `inferred`
[[nodiscard]] inline int after_resistance(int damage, int resistance) noexcept {
    if (resistance == data::kResistanceImmune) {
        return 0;
    }
    if (resistance <= 0) {
        return damage;
    }
    const int through = damage - damage * resistance / 100;
    return through < 1 ? 1 : through;
}

// The dice a character swings for: whatever is in their weapon slot, or a
// fist. Before there were slots this took the first weapon in the pack, which
// meant picking something up could change what you were fighting with.
[[nodiscard]] inline data::Dice weapon_of(const Character& who, const data::ItemStatsTable& items) {
    const int held = who.equipped[static_cast<std::size_t>(Slot::Weapon)];
    if (held > 0 && !who.equipped_broken[static_cast<std::size_t>(Slot::Weapon)]) {
        if (const auto* row = items.at(static_cast<std::size_t>(held)); row != nullptr) {
            if (const data::Dice dice = data::parse_dice(row->modifier_1); !dice.empty()) {
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
        if (row == nullptr || data::parse_dice(row->modifier_1).empty() == false) {
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

    // Share it among whoever is still standing, the way a party splits a kill.
    // Nobody standing means nobody collects, and it waits. `inferred`
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
        const int each = (experience_ + experience_ * bonus_percent / 100) / standing;
        for (auto& who : party) {
            if (who.hit_points > 0) {
                who.experience += each;
            }
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
                       std::string_view rider_element = {}) {
        if (!alive(actor) || who.hit_points <= 0) {
            return {};
        }
        const auto id = static_cast<std::size_t>(session.actors[actor].monster_id);
        if (id == 0 || id > monsters.entries().size()) {
            return {};
        }
        const auto& monster = monsters.entries()[id - 1];

        // Whether it lands: the character's accuracy against the monster's
        // armour class, on a hundred. `inferred`
        const int aim = 50 + attribute_bonus(who.attribute(Attribute::Accuracy)) * 5 +
                        skill.to_hit - monster.armor_class;
        if (static_cast<int>(random_.next() % 100) >= aim) {
            return who.name + " misses " + monster.name;
        }

        // A weapon does physical damage, which no resistance column answers;
        // the call is here so an elemental one would be answered correctly.
        int damage = data::roll(weapon_of(who, items), random_) +
                     attribute_bonus(who.attribute(Attribute::Might)) + skill.damage;
        std::string flourish;
        // A blade in the left hand — the Shield slot, opened by its skill's
        // own "in left hand" line — swings its own dice alongside.
        if (const int off = who.equipped[static_cast<std::size_t>(Slot::Shield)];
            off > 0 && !who.equipped_broken[static_cast<std::size_t>(Slot::Shield)]) {
            if (const auto* row = items.at(static_cast<std::size_t>(off)); row != nullptr) {
                if (const data::Dice dice = data::parse_dice(row->modifier_1); !dice.empty()) {
                    damage += data::roll(dice, random_);
                    flourish = ", both hands";
                }
            }
        }
        // "Bow fires two arrows on every attack": a second roll of the
        // same weapon joins the blow.
        if (skill.second_arrow) {
            damage += data::roll(weapon_of(who, items), random_);
            flourish = ", twice-feathered";
        }
        // "Chance to cause triple damage equal to skill."
        if (skill.triple_percent > 0 &&
            static_cast<int>(random_.next() % 100) < skill.triple_percent) {
            damage *= 3;
            flourish = ", a vicious strike";
        }
        damage = after_resistance(damage < 1 ? 1 : damage, resistance_to(monster, "Phys"));
        damage = damage < 1 ? 1 : damage;
        // A weapon's elemental rider — "Adds 6-8 points of Cold damage" —
        // rolls apart and is answered by its own element.
        if (!rider.empty()) {
            damage += after_resistance(data::roll(rider, random_),
                                       resistance_to(monster, rider_element));
        }

        // "Chance to stun equal to skill": the stunned lose their next
        // moment — a second on their recovery is the engine's own length.
        if (skill.stun_percent > 0 && alive(actor) &&
            static_cast<int>(random_.next() % 100) < skill.stun_percent) {
            combatants_[actor].recovery += 1.0f;
            flourish += ", stunned";
        }
        std::string what = land(actor, damage, monster, who.name, items, random_items,
                                standard_bonuses, special_bonuses);
        return flourish.empty() ? what : what + flourish;
    }

    // A spell's blow at one monster: the prose's flat part, plus one roll of
    // the scaling part per point of skill, answered by the resistance of the
    // spell's own element.
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
            c.recovery -= dt;
            if (c.recovery > 0.0f) {
                continue;
            }
            // The afflicted hold their blows: the fearful flee, the held
            // cannot retaliate, the calmed have no hostile feelings.
            if (c.feared > 0.0f || c.paralyzed > 0.0f || c.charmed > 0.0f) {
                continue;
            }
            const auto& actor = session.actors[i];
            const float dx = actor.position.x - eye.x;
            const float dz = actor.position.z - eye.z;
            const float range2 = dx * dx + dz * dz;
            const auto id = static_cast<std::size_t>(actor.monster_id);
            if (id == 0 || id > monsters.entries().size()) {
                continue;
            }
            const auto& monster = monsters.entries()[id - 1];
            // Past arm's reach, only a monster whose Miss column names a
            // missile attacks — and its shot is reported for the caller to
            // fly.
            const bool in_melee = range2 <= kMeleeRange * kMeleeRange;
            if (!in_melee &&
                (range2 > kMissileRange * kMissileRange || !has_missile(monster))) {
                continue;
            }
            // Slow "doubles the recovery rate of a single monster".
            c.recovery = static_cast<float>(monster.recovery) * kMonsterRecoveryScale *
                         (c.slowed > 0.0f ? 2.0f : 1.0f);

            if (std::string what = swing(monster, spells, party, now); !what.empty()) {
                noises_.push_back({i, 0});
                if (!in_melee) {
                    shots_.push_back(i);
                }
                last = std::move(what);
            }
        }
        return last;
    }

private:
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

        // The table's own spell first: cast as often as the row's percent
        // says, at its written mastery and skill — the number the prose's
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
                    std::string what = monster.name + " casts " + spell->name + " at " +
                                       target.name + " for " + std::to_string(damage);
                    what += fell(target, was_down, damage);
                    return what;
                }
            }
        }

        // Which attack swings: the "Att%" column is the second attack's
        // chance — the header groups it with the first, but its 10..30
        // values sit exactly on the rows whose second attack is the rare
        // elemental bite, and a first-attack share of 20 would invert every
        // such monster. `observed` for the values, `inferred` for the
        // reading. The parser leaves that column on the first slot.
        const data::Dice second = data::parse_dice(monster.attacks[1].damage);
        const bool use_second =
            !second.empty() && monster.attacks[0].chance > 0 &&
            static_cast<int>(random_.next() % 100) < monster.attacks[0].chance;
        for (const auto& attack :
             {use_second ? monster.attacks[1] : monster.attacks[0], monster.attacks[0]}) {
            const data::Dice dice = data::parse_dice(attack.damage);
            if (dice.empty()) {
                continue;
            }
            // Its chance to land, against the character's armour class.
            const int aim = 50 - target.armor_class;
            if (static_cast<int>(random_.next() % 100) >= aim) {
                return monster.name + " misses " + target.name;
            }
            const int damage =
                after_resistance(data::roll(dice, random_), resistance_to(target, attack.type));
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
                } else if (bonus == "Asleep" || bonus == "Affraid" || bonus == "Weak" ||
                           bonus == "Drunk" || bonus == "Insane" || bonus == "Paralyze" ||
                           bonus.substr(0, 5) == "Curse") {
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
    int experience_ = 0;
    int gold_ = 0;
    std::vector<data::GeneratedItem> loot_;
    int stolen_ = 0;
    data::ArtifactGenerationState artifacts_;
    Mm6Random random_{1};
};

// Which living monster the party is aiming at and can reach, or kNoActor.
// Ties go to whatever is most directly in front, the way the inspect panel
// chooses what to name.
[[nodiscard]] inline std::size_t aimed_actor(const world::MapSession& session, const Battle& battle,
                                             const render::Vec3& eye, const render::Vec3& forward,
                                             float reach) {
    std::size_t best = kNoActor;
    float best_aim = 0.5f;  // anything less is not being aimed at
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
        const float aim = (d.x * forward.x + d.y * forward.y + d.z * forward.z) / distance;
        if (aim > best_aim) {
            best_aim = aim;
            best = i;
        }
    }
    return best;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_COMBAT_HPP
