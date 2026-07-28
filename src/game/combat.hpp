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
#include <vector>

#include "core/data/dice.hpp"
#include "core/data/item_generation.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/treasure.hpp"
#include "core/random.hpp"
#include "core/world/map_session.hpp"
#include "game/inventory.hpp"
#include "game/party.hpp"

namespace starhaven::game {

// How near a monster has to be to swing at the party, in world units.
// `inferred`
inline constexpr float kMeleeRange = 400.0f;

// And how near the party has to be to swing back. The same, so that whoever
// closes the distance can be answered.
inline constexpr float kPartyReach = kMeleeRange;

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
};

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

// And the same question of a character.
[[nodiscard]] inline int resistance_to(const Character& who, std::string_view type) noexcept {
    const int which = resistance_index(type);
    return which < 0 ? 0 : who.resistances[static_cast<std::size_t>(which)];
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
    if (held > 0) {
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
    for (const int id : who.equipped) {
        if (id <= 0) {
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

    // And the gold, which is the party's rather than any one character's.
    [[nodiscard]] int unclaimed_gold() const noexcept { return gold_; }
    int take_gold() noexcept {
        const int taken = gold_;
        gold_ = 0;
        return taken;
    }

    // The items kills have left and nobody has picked up yet.
    [[nodiscard]] const std::vector<int>& unclaimed_loot() const noexcept { return loot_; }
    std::vector<int> take_loot() {
        std::vector<int> taken = std::move(loot_);
        loot_.clear();
        return taken;
    }

    // Share it among whoever is still standing, the way a party splits a kill.
    // Nobody standing means nobody collects, and it waits. `inferred`
    void award(std::array<Character, 4>& party) {
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
        const int each = experience_ / standing;
        for (auto& who : party) {
            if (who.hit_points > 0) {
                who.experience += each;
            }
        }
        experience_ = 0;
    }

    // The party strikes one monster. Returns what happened, for the message
    // line, or empty when the blow was not possible at all.
    std::string strike(std::size_t actor, Character& who, const Pack& pack,  // NOLINT
                       const world::MapSession& session, const data::MonsterStatsTable& monsters,
                       const data::ItemStatsTable& items,
                       const data::RandomItemTable& random_items,
                       const data::StandardBonusTable& standard_bonuses,
                       const data::SpecialBonusTable& special_bonuses) {
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
        const int aim =
            50 + attribute_bonus(who.attribute(Attribute::Accuracy)) * 5 - monster.armor_class;
        if (static_cast<int>(random_.next() % 100) >= aim) {
            return who.name + " misses " + monster.name;
        }

        // A weapon does physical damage, which no resistance column answers;
        // the call is here so an elemental one would be answered correctly.
        int damage = data::roll(weapon_of(who, items), random_) +
                     attribute_bonus(who.attribute(Attribute::Might));
        damage = after_resistance(damage < 1 ? 1 : damage, resistance_to(monster, "Phys"));
        damage = damage < 1 ? 1 : damage;

        Combatant& target = combatants_[actor];
        target.hit_points -= damage;
        target.wince = kWinceSeconds;
        std::string what = who.name + " hits " + monster.name + " for " + std::to_string(damage);
        if (target.hit_points <= 0) {
            target.alive = false;
            target.hit_points = 0;
            target.wince = 0.0f;
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
                    loot_.push_back(rolled.item_id);
                }
            }
            what += " and kills it";
        }
        return what;
    }

    // The monsters take their turn. Anything alive and in reach swings at a
    // character who is still standing.
    std::string update(float dt, const world::MapSession& session,
                       const data::MonsterStatsTable& monsters, std::array<Character, 4>& party,
                       const render::Vec3& eye) {
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
            c.recovery -= dt;
            if (c.recovery > 0.0f) {
                continue;
            }
            const auto& actor = session.actors[i];
            const float dx = actor.position.x - eye.x;
            const float dz = actor.position.z - eye.z;
            if (dx * dx + dz * dz > kMeleeRange * kMeleeRange) {
                continue;
            }
            const auto id = static_cast<std::size_t>(actor.monster_id);
            if (id == 0 || id > monsters.entries().size()) {
                continue;
            }
            const auto& monster = monsters.entries()[id - 1];
            c.recovery = static_cast<float>(monster.recovery) * kMonsterRecoveryScale;

            if (std::string what = swing(monster, party); !what.empty()) {
                last = std::move(what);
            }
        }
        return last;
    }

private:
    // One monster's blow at whoever in the party is still standing.
    std::string swing(const data::MonsterStatsEntry& monster, std::array<Character, 4>& party) {
        std::vector<std::size_t> standing;
        for (std::size_t i = 0; i < party.size(); ++i) {
            if (party[i].hit_points > 0) {
                standing.push_back(i);
            }
        }
        if (standing.empty()) {
            return {};
        }
        Character& target = party[standing[random_.next() % standing.size()]];

        // The first attack it has dice for; the second is a spell or a missile
        // more often than not.
        for (const auto& attack : monster.attacks) {
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
            if (target.hit_points <= 0) {
                target.hit_points = 0;
                what += " and drops them";
            }
            return what;
        }
        return {};
    }

    std::vector<Combatant> combatants_;
    int experience_ = 0;
    int gold_ = 0;
    std::vector<int> loot_;
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
