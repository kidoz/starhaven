// A sitting, played without a window.
//
// The engine's numbers keep changing under traces — the attribute ladder, the
// recovery table, the clock's rate, the weapon specials — and until now the
// only things exercising them were unit tests over constants and a bench that
// renders frames without a fight. This runs the parts that decide what a
// character *is*: it makes a party, opens a real map, sets the party in among
// the actors it places, and steps the world loop for a stretch of game time
// while everyone strikes what is nearest. Then it says what happened.
//
// Deterministic: the party seed, the battle seed and the fixed timestep are
// all arguments, and no clock but the game's own is read. The same command
// gives the same report every run.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "core/assets/asset_cache.hpp"
#include "core/data/game_data.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/name_table.hpp"
#include "core/data/spell_stats.hpp"
#include "core/data/text_table.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_session.hpp"
#include "game/clock.hpp"
#include "game/combat.hpp"
#include "game/inventory.hpp"
#include "game/party.hpp"
#include "game/player.hpp"
#include "game/skills.hpp"
#include "game/spell_damage.hpp"
#include "game/spell_switch.hpp"

using namespace starhaven;

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [<map>] [--minutes N] [--seed N] [--fps N]\n"
              << "          [--still] [--teach] [--no-spells] [--verbose]\n"
              << "\n"
              << "Plays a sitting with no window: a starting party against the\n"
              << "actors a real map places, for N minutes of world time.\n"
              << "Reports what a starting character is, and what the fight did\n"
              << "to it. The party closes on whatever is nearest and alive;\n"
              << "--still holds it where it started. Default map OutC3.Odm,\n"
              << "30 world minutes, seed 7.\n"
              << "\n"
              << "Set STARHAVEN_GAME_DIR to the install directory.\n";
}

// The nearest living actor within `reach`, or none. The reach matters: the
// game gives the party the same 400 units the monsters' own melee uses, and
// an earlier version of this harness let the party strike at any distance —
// which made it look as though the monsters never fought back, when in fact
// the party was simply hitting things that could not reach it.
std::size_t nearest_alive(const world::MapSession& session, const game::Battle& battle,
                          const render::Vec3& eye, float reach = 0.0F) {
    std::size_t best = game::kNoActor;
    float closest = 0.0F;
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        if (!battle.alive(i)) {
            continue;
        }
        const render::Vec3 d{session.actors[i].position.x - eye.x,
                             session.actors[i].position.y - eye.y,
                             session.actors[i].position.z - eye.z};
        const float range = d.x * d.x + d.y * d.y + d.z * d.z;
        if (reach > 0.0F && range > reach * reach) {
            continue;
        }
        if (best == game::kNoActor || range < closest) {
            best = i;
            closest = range;
        }
    }
    return best;
}

struct Tally {
    int casts = 0;
    int spell_damage = 0;
    int swings = 0;
    int landed = 0;
    int dealt = 0;
    int taken = 0;
    int low_water = 0;
    std::int64_t down_at = -1;  // world minute the character first fell
};

}  // namespace

int main(int argc, char** argv) {
    std::string map = "OutC3.Odm";
    int minutes = 30;
    std::uint32_t seed = 7;
    int fps = 20;
    bool verbose = false;
    bool advance = true;
    bool casting = true;
    bool teach = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--minutes" && i + 1 < argc) {
            minutes = std::atoi(argv[++i]);
        } else if (a == "--seed" && i + 1 < argc) {
            seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--fps" && i + 1 < argc) {
            fps = std::max(1, std::atoi(argv[++i]));
        } else if (a == "--teach") {
            teach = true;
        } else if (a == "--no-spells") {
            casting = false;
        } else if (a == "--still") {
            advance = false;
        } else if (a == "--verbose") {
            verbose = true;
        } else if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (a.rfind("--", 0) == 0) {
            print_usage(argv[0]);
            return 2;
        } else {
            map = a;
        }
    }

    const auto install = platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << platform::kInstallEnvVar << "\n";
        return 1;
    }
    const std::filesystem::path data_dir = *install / "data";

    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: could not load MONSTERS.TXT\n";
        return 1;
    }
    data::ItemStatsTable items;
    (void)data::load_item_stats(data_dir, items);
    data::RandomItemTable random_items;
    (void)data::load_random_items(data_dir, random_items);
    data::StandardBonusTable standard_bonuses;
    (void)data::load_standard_bonuses(data_dir, standard_bonuses);
    data::SpecialBonusTable special_bonuses;
    (void)data::load_special_bonuses(data_dir, special_bonuses);
    data::SpellStatsTable spells;
    (void)data::load_spell_stats(data_dir, spells);
    data::NameTable names;
    (void)data::load_names(data_dir, names);

    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    if (world::load_map_session(game::resolve_games_lod(), data_dir, map, cache, session) !=
        world::MapSessionError::None) {
        std::cerr << "error: could not open " << map << "\n";
        return 1;
    }

    std::array<game::Character, 4> party = game::make_party(names, seed);
    if (teach) {
        // A starting party knows only First Aid, so nothing aimed is ever
        // cast. These four are the cheapest aimed spells in the game, one
        // per element, and they put the traced dice under load without
        // pretending a level-one party would have them.
        for (auto& who : party) {
            if (who.max_spell_points <= 0) {
                continue;
            }
            for (const int id : {2, 24, 35, 45}) {
                who.known_spells.insert(id);
            }
        }
    }
    std::array<game::Pack, 4> packs;
    game::Battle battle;
    battle.reset(session, monsters, seed);

    std::cout << "A sitting on " << map << ", seed " << seed << ", " << minutes
              << " world minutes at " << fps << " steps a second\n\n";
    std::cout << "What a starting character is\n";
    for (const auto& who : party) {
        std::cout << "  " << std::setw(12) << std::left << who.name << std::setw(10)
                  << who.class_name << " hp " << std::setw(4) << who.max_hit_points << " sp "
                  << std::setw(4) << who.max_spell_points << " ac " << std::setw(3)
                  << who.armor_class << "  might " << who.attribute(game::Attribute::Might)
                  << " (" << std::showpos << game::attribute_bonus(
                                                 who.attribute(game::Attribute::Might))
                  << std::noshowpos << ")"
                  << "  speed " << who.attribute(game::Attribute::Speed) << " ("
                  << std::showpos
                  << game::attribute_bonus(who.attribute(game::Attribute::Speed))
                  << std::noshowpos << ")"
                  << "  end " << who.attribute(game::Attribute::Endurance) << "\n";
    }

    // Stand where the actors are, so there is something in reach. The party
    // walks to whatever is nearest and alive, which is what a player does and
    // what keeps a fight going once the first knot is cleared.
    render::Vec3 eye{};
    if (!session.actors.empty()) {
        eye = session.actors.front().position;
    }

    const float dt = 1.0F / static_cast<float>(fps);
    const int steps = minutes * 60 * fps /
                      static_cast<int>(game::kWorldSecondsPerSecond);  // world minutes → steps
    game::GameClock clock{0};
    std::array<float, 4> recovery{};
    std::array<Tally, 4> tally;
    for (std::size_t i = 0; i < party.size(); ++i) {
        tally[i].low_water = party[i].max_hit_points;
    }
    int killed = 0;
    int replies = 0;
    int levels = 0;
    // The wearing-down the time-advance routine applies: a counter that
    // climbs an hour at a time and lays Weak past its second.
    int hours_awake = 0;
    std::int64_t fatigue_hour = 0;
    std::int64_t weak_at = -1;
    const std::size_t actors_at_start = session.actors.size();

    for (int step = 0; step < steps; ++step) {
        clock.advance_seconds(dt);
        // Close on the nearest living thing, so the party is always in a
        // fight rather than standing over the last body.
        if (const std::size_t near = advance ? nearest_alive(session, battle, eye)
                                             : game::kNoActor;
            near != game::kNoActor) {
            eye = session.actors[near].position;
        }
        const std::array<int, 4> before{party[0].hit_points, party[1].hit_points,
                                        party[2].hit_points, party[3].hit_points};

        for (std::size_t who = 0; who < party.size(); ++who) {
            recovery[who] = recovery[who] > dt ? recovery[who] - dt : 0.0F;
            if (party[who].hit_points <= 0 || recovery[who] > 0.0F) {
                continue;
            }
            const std::size_t target =
                nearest_alive(session, battle, eye, game::kPartyReach);
            if (target == game::kNoActor) {
                continue;
            }
            // A caster with points to spend throws its best aimed spell
            // before reaching for a fist: the traced dice, the spell-point
            // purse and the aimed list all under load at once. The skill is
            // a flat five throughout — a stand-in, since a starting party
            // has one point and the point of the exercise is the dice.
            if (casting && !party[who].known_spells.empty() && party[who].spell_points > 0) {
                int chosen = 0;
                int best = 0;
                for (const int id : party[who].known_spells) {
                    const auto* row = spells.at(static_cast<std::size_t>(id));
                    if (row == nullptr || !game::spell_is_aimed(id) ||
                        party[who].spell_points < row->cost_normal) {
                        continue;
                    }
                    const int worth = game::roll_spell_damage(id, 5, [] { return 3ULL; });
                    if (worth > best) {
                        best = worth;
                        chosen = id;
                    }
                }
                if (chosen > 0) {
                    const auto* row = spells.at(static_cast<std::size_t>(chosen));
                    party[who].spell_points -= row->cost_normal;
                    data::SpellRange flat;
                    data::SpellRange scaled;
                    (void)game::traced_damage_ranges(chosen, 5, flat, scaled);
                    const int hp_was = battle.health_of(target).first;
                    battle.smite(target, flat, scaled, 5, row->element, party[who].name, session,
                                 monsters, items, random_items, standard_bonuses,
                                 special_bonuses);
                    ++tally[who].casts;
                    tally[who].spell_damage += hp_was - battle.health_of(target).first;
                    recovery[who] = game::recovery_seconds(game::kBareHandRecovery);
                    continue;
                }
            }
            const int monster_hp_before = battle.health_of(target).first;
            const std::string said =
                battle.strike(target, party[who], packs[who], session, monsters, items,
                              random_items, standard_bonuses, special_bonuses);
            if (said.empty()) {
                continue;
            }
            ++tally[who].swings;
            const int after = battle.health_of(target).first;
            if (after < monster_hp_before) {
                ++tally[who].landed;
                tally[who].dealt += monster_hp_before - after;
            }
            if (!battle.alive(target) && monster_hp_before > 0) {
                ++killed;
            }
            if (verbose && tally[who].swings <= 3) {
                std::cout << "    " << clock.hhmm() << "  " << said << "\n";
            }
            // The traced cost of the blow, in Rec points.
            int points = game::kBareHandRecovery;
            const auto wi = static_cast<std::size_t>(game::Slot::Weapon);
            if (const int held = party[who].equipped[wi];
                held > 0 && !party[who].equipped_broken[wi]) {
                if (const auto* row = items.at(static_cast<std::size_t>(held));
                    row != nullptr && !row->skill_group.empty()) {
                    points = game::gear_recovery(row->skill_group);
                }
            }
            points -= game::attribute_bonus(party[who].attribute(game::Attribute::Speed));
            recovery[who] = game::recovery_seconds(std::max(0, points));
        }

        const std::string back =
            battle.update(dt, session, monsters, spells, party, eye, clock.minutes());
        battle.recruit(session, monsters);
        battle.award(party);
        if (const std::int64_t hour_now = clock.minutes() / game::kMinutesPerHour;
            hour_now != fatigue_hour) {
            hours_awake += static_cast<int>(hour_now - fatigue_hour);
            fatigue_hour = hour_now;
            if (hours_awake >= game::kFatigueWeakAfterHours) {
                for (auto& who : party) {
                    if (who.affliction.empty() && who.hit_points > 0) {
                        who.affliction = "Weak";
                        if (weak_at < 0) {
                            weak_at = clock.minutes();
                        }
                    }
                }
            }
        }
        for (auto& who : party) {
            levels += game::level_up(who);
        }
        if (verbose && !back.empty() && replies < 4) {
            std::cout << "    " << clock.hhmm() << "  " << back << "\n";
            ++replies;
        }

        for (std::size_t who = 0; who < party.size(); ++who) {
            if (party[who].hit_points < before[who]) {
                tally[who].taken += before[who] - party[who].hit_points;
            }
            tally[who].low_water = std::min(tally[who].low_water, party[who].hit_points);
            if (party[who].hit_points <= 0 && tally[who].down_at < 0) {
                tally[who].down_at = clock.minutes();
            }
        }
    }

    const double real_minutes =
        static_cast<double>(steps) / static_cast<double>(fps) / 60.0;
    std::cout << "\nAfter " << minutes << " world minutes (" << real_minutes
              << " real, " << steps << " steps) among " << actors_at_start << " actors\n";
    for (std::size_t who = 0; who < party.size(); ++who) {
        const Tally& t = tally[who];
        const double per_minute =
            minutes > 0 ? static_cast<double>(t.swings) / static_cast<double>(minutes) : 0.0;
        std::cout << "  " << std::setw(12) << std::left << party[who].name << " swung "
                  << std::setw(4) << t.swings << " (" << std::fixed << std::setprecision(1)
                  << per_minute << "/world minute), landed " << std::setw(4) << t.landed;
        if (t.swings > 0) {
            std::cout << " (" << 100 * t.landed / t.swings << "%)";
        }
        std::cout << ", dealt " << std::setw(5) << t.dealt << ", took " << std::setw(5)
                  << t.taken << ", low " << std::setw(4) << t.low_water << " of "
                  << party[who].max_hit_points << ", level " << party[who].level << ", xp " << party[who].experience
                  << ", cast " << t.casts << " for " << t.spell_damage;
        if (t.down_at >= 0) {
            std::cout << "  DOWN at world minute " << t.down_at;
        }
        std::cout << "\n";
    }
    std::cout << "  awake " << hours_awake << " world hours; ";
    if (weak_at >= 0) {
        std::cout << "Weak laid on at world minute " << weak_at << "\n";
    } else {
        std::cout << "nobody went Weak\n";
    }
    std::cout << "  " << levels << " levels gained; " << killed << " actors killed; "
              << battle.unclaimed_experience() << " experience, " << battle.take_gold() << " gold\n";
    return 0;
}
