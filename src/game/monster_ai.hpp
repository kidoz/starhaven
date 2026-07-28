#ifndef STARHAVEN_GAME_MONSTER_AI_HPP
#define STARHAVEN_GAME_MONSTER_AI_HPP

// Monsters that move, and that are drawn from the side you see them from.
//
// The frame table gives five views of a directional sprite and the monster
// table gives how far and how fast each kind moves; see docs/formats/dsft.md
// and docs/formats/text-tables.md.

#include <cmath>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "core/data/monster_stats.hpp"
#include "core/random.hpp"
#include "core/render/math3d.hpp"
#include "core/world/map_session.hpp"

namespace starhaven::game {

// How one kind of monster moves, from its `MONSTERS.TXT` row.
struct MonsterMotion {
    float speed = 0.0f;   // world units per second
    float roam = 0.0f;    // how far from where it started it will wander
    float notice = 0.0f;  // how near the party has to be for it to react
    bool flees = false;   // a Wimp runs the other way

    [[nodiscard]] bool still() const noexcept { return speed <= 0.0f; }
};

// How far each of the table's four movement words lets a monster wander. The
// table gives the word, not a distance, so these are this engine's reading of
// it: one, two, four and eight terrain tiles. `inferred`
inline constexpr float kRoamShort = 512.0f;
inline constexpr float kRoamMedium = 1024.0f;
inline constexpr float kRoamLong = 2048.0f;
inline constexpr float kRoamFree = 4096.0f;

// And how near the party gets before each kind of AI reacts. Also a reading of
// words rather than a number the table gives. `inferred`
inline constexpr float kNoticeAggressive = 3000.0f;
inline constexpr float kNoticeNormal = 1500.0f;
inline constexpr float kNoticeWimp = 1000.0f;

// The table's speed column is a bare number — 120 to 300 across the 173
// monsters — with no unit. Treating it as world units per second puts the
// fastest at half the party's pace, which is the relation the original has.
// `inferred`
inline constexpr float kSpeedScale = 1.0f;

[[nodiscard]] inline MonsterMotion motion_for(const data::MonsterStatsEntry& monster) {
    MonsterMotion out;
    out.speed = static_cast<float>(monster.speed) * kSpeedScale;

    if (monster.movement == "Short") {
        out.roam = kRoamShort;
    } else if (monster.movement == "Med") {
        out.roam = kRoamMedium;
    } else if (monster.movement == "Long") {
        out.roam = kRoamLong;
    } else if (monster.movement == "Free") {
        out.roam = kRoamFree;
    }

    // Nine of the 173 have a hostility of 0 and every other one has 4. Those
    // nine keep to themselves.
    if (monster.hostility > 0) {
        if (monster.ai_type == "Aggress" || monster.ai_type == "Suicidal") {
            out.notice = kNoticeAggressive;
        } else if (monster.ai_type == "Normal") {
            out.notice = kNoticeNormal;
        } else if (monster.ai_type == "Wimp") {
            out.notice = kNoticeWimp;
            out.flees = true;
        }
    }
    return out;
}

// Which of a directional sprite's five views to draw, and whether to mirror
// it.
struct SpriteView {
    int index = 0;
    bool mirror = false;
};

// `facing` is the direction the thing faces and `to_viewer` the direction from
// it to the eye, both as angles in the renderer's ground plane.
//
// The five views are angles relative to the viewer, not compass headings: view
// 0 is the front, view 4 the back, view 2 the profile, and the other half of
// the circle is the same five mirrored. Measured, not assumed — see
// docs/formats/dsft.md. Which side is mirrored is `inferred`.
[[nodiscard]] inline SpriteView sprite_view(float facing, float to_viewer) noexcept {
    constexpr float kPi = 3.14159265f;
    float relative = to_viewer - facing;
    while (relative > kPi) {
        relative -= 2.0f * kPi;
    }
    while (relative < -kPi) {
        relative += 2.0f * kPi;
    }
    const float step = kPi / 4.0f;  // five views spanning half a turn
    int index = static_cast<int>(std::lround(std::fabs(relative) / step));
    index = index < 0 ? 0 : (index > 4 ? 4 : index);
    return {index, relative < 0.0f};
}

// How long a monster keeps one heading before choosing another, in seconds.
inline constexpr float kWanderInterval = 3.0f;

// How near a monster and the party come to each other before one is pushed
// back out: a monster's radius plus a body's. `inferred`
inline constexpr float kPartySpacing = 96.0f;

// Move `at` out of a circle of `radius` around `centre`, if it is inside.
[[nodiscard]] inline render::Vec3 push_out_of(render::Vec3 at, const render::Vec3& centre,
                                              float radius) noexcept {
    const float dx = at.x - centre.x;
    const float dz = at.z - centre.z;
    const float squared = dx * dx + dz * dz;
    if (squared >= radius * radius) {
        return at;
    }
    // Dead centre has no direction to leave by; any one will do.
    const float length = std::sqrt(squared);
    const float nx = length > 0.001f ? dx / length : 1.0f;
    const float nz = length > 0.001f ? dz / length : 0.0f;
    at.x = centre.x + nx * radius;
    at.z = centre.z + nz * radius;
    return at;
}

// How wide a monster is, and how tall, for the purpose of not walking through
// things. The monster table gives neither. `inferred`
inline constexpr float kMonsterRadius = 48.0f;
inline constexpr float kMonsterHeight = 160.0f;

// How far apart two monsters keep. Less than twice the radius, because a crowd
// that cannot overlap at all jams in a corridor. `inferred`
inline constexpr float kMonsterSpacing = 72.0f;

// How far from the party a monster still tests itself against the level's
// walls. Sweeping four hundred monsters against three thousand polygons costs
// five milliseconds a frame; sweeping the dozen you could see costs nothing,
// and one that has drifted into a wall while you were elsewhere is pushed out
// on the step after you come near. `inferred`
inline constexpr float kWallTestRange = 6000.0f;

// The moving monsters of one loaded map.
//
// The map's event file has room for a facing and a velocity per actor, and
// ships them zeroed on every actor of every map, so movement is entirely this
// engine's: nothing is being contradicted, and nothing is being reproduced
// either. See docs/formats/event-actors.md.
class Mob {
public:
    // Take the session's actors as they stand: where each one is now is where
    // it belongs, and what it is decides how it moves.
    void reset(const world::MapSession& session, const data::MonsterStatsTable& monsters,
               std::uint32_t seed) {
        states_.clear();
        random_ = Mm6Random{seed};
        states_.reserve(session.actors.size());
        recruit(session, monsters);
    }

    // Take in actors appended after the reset — what a summon adds.
    void recruit(const world::MapSession& session, const data::MonsterStatsTable& monsters) {
        for (std::size_t i = states_.size(); i < session.actors.size(); ++i) {
            const auto& actor = session.actors[i];
            State s;
            s.home = actor.position;
            s.facing = angle_of(random_.next());
            s.timer = static_cast<float>(random_.next() % 1000) / 1000.0f * kWanderInterval;
            const auto id = static_cast<std::size_t>(actor.monster_id);
            if (actor.monster_id > 0 && id <= monsters.entries().size()) {
                s.motion = motion_for(monsters.entries()[id - 1]);
            }
            states_.push_back(s);
        }
    }

    // Move everything one step. The session's actor positions are what change.
    //
    // `is_alive` says which actors still move; a caller with no fight going on
    // can pass one that always says yes.
    template <typename AliveFn>
    void update(float dt, world::MapSession& session, const render::Vec3& party, AliveFn is_alive) {
        if (states_.size() != session.actors.size() || dt <= 0.0f) {
            return;
        }
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (!is_alive(i)) {
                continue;
            }
            step(dt, states_[i], session.actors[i], session, party);
        }
        separate(session, party, is_alive);
    }

    void update(float dt, world::MapSession& session, const render::Vec3& party) {
        update(dt, session, party, [](std::size_t) { return true; });
    }

    [[nodiscard]] float facing(std::size_t actor) const noexcept {
        return actor < states_.size() ? states_[actor].facing : 0.0f;
    }
    [[nodiscard]] std::size_t size() const noexcept { return states_.size(); }

private:
    struct State {
        render::Vec3 home;
        float facing = 0.0f;
        float timer = 0.0f;
        bool moving = true;
        MonsterMotion motion;
    };

    static float angle_of(std::uint16_t r) noexcept {
        constexpr float kTwoPi = 6.2831853f;
        return static_cast<float>(r % 4096) / 4096.0f * kTwoPi;
    }

    // Nobody stands inside anybody. Monsters are bucketed by terrain tile so
    // that a map with four hundred of them does not cost four hundred squared
    // comparisons a frame.
    template <typename AliveFn>
    void separate(world::MapSession& session, const render::Vec3& party, AliveFn is_alive) {
        buckets_.clear();
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (!is_alive(i)) {
                continue;
            }
            auto& actor = session.actors[i];
            actor.position = push_out(actor.position, party, kPartySpacing);
            buckets_[key_of(actor.position)].push_back(i);
        }

        for (const auto& [key, here] : buckets_) {
            for (const std::size_t i : here) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto neighbour =
                            buckets_.find(key + static_cast<std::int64_t>(dz) * kBucketStride +
                                          static_cast<std::int64_t>(dx));
                        if (neighbour == buckets_.end()) {
                            continue;
                        }
                        for (const std::size_t j : neighbour->second) {
                            if (j <= i) {
                                continue;
                            }
                            session.actors[i].position =
                                push_out(session.actors[i].position, session.actors[j].position,
                                         kMonsterSpacing);
                        }
                    }
                }
            }
        }
    }

    // One bucket per terrain tile, flattened into a single integer.
    static constexpr std::int64_t kBucketStride = 1 << 20;
    static std::int64_t key_of(const render::Vec3& at) noexcept {
        const auto x = static_cast<std::int64_t>(std::floor(at.x / 512.0f));
        const auto z = static_cast<std::int64_t>(std::floor(at.z / 512.0f));
        return z * kBucketStride + x;
    }

    static render::Vec3 push_out(render::Vec3 at, const render::Vec3& centre,
                                 float radius) noexcept {
        return push_out_of(at, centre, radius);
    }

    static float distance_xz(const render::Vec3& a, const render::Vec3& b) noexcept {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    void step(float dt, State& state, world::SessionActor& actor, const world::MapSession& session,
              const render::Vec3& party) {
        if (state.motion.still()) {
            return;
        }
        const float to_party = distance_xz(actor.position, party);

        if (state.motion.notice > 0.0f && to_party < state.motion.notice) {
            // Toward the party, or directly away from it.
            const float bearing =
                std::atan2(party.z - actor.position.z, party.x - actor.position.x);
            constexpr float kPi = 3.14159265f;
            state.facing = state.motion.flees ? bearing + kPi : bearing;
            state.moving = true;
            // Close enough is close enough: a monster that keeps walking ends
            // up standing inside the party.
            if (!state.motion.flees && to_party < kStandOff) {
                state.moving = false;
            }
        } else {
            state.timer -= dt;
            if (state.timer <= 0.0f) {
                state.timer = kWanderInterval;
                state.moving = random_.next() % 4 != 0;  // three steps in four
                state.facing = angle_of(random_.next());
            }
            // Wandering has a leash: past it, the next heading is homeward.
            if (distance_xz(actor.position, state.home) > state.motion.roam) {
                state.facing =
                    std::atan2(state.home.z - actor.position.z, state.home.x - actor.position.x);
                state.moving = true;
            }
        }

        if (!state.moving) {
            return;
        }
        const float distance = state.motion.speed * dt;
        const render::Vec3 from = actor.position;
        render::Vec3 to{from.x + std::cos(state.facing) * distance, from.y,
                        from.z + std::sin(state.facing) * distance};

        // Walls and buildings: the same swept cylinder the party uses, but
        // only for monsters near enough to be seen doing it.
        if (to_party < kWallTestRange) {
            to = session.collision.slide(from, to, kMonsterRadius, kMonsterHeight);
        }

        // Trees, rocks and barrels: the map's own list of what stands near
        // this tile, and each kind's own radius.
        session.decorations_near(to.x, to.z, nearby_);
        for (const std::size_t id : nearby_) {
            const auto& decoration = session.decorations[id];
            if (decoration.radius == 0) {
                continue;
            }
            to = push_out(to, decoration.position,
                          static_cast<float>(decoration.radius) + kMonsterRadius);
        }

        actor.position = to;
        if (session.outdoor()) {
            actor.position.y = session.terrain_height_at(actor.position.x, actor.position.z);
        }
    }

    // How near a monster comes before it stops walking into you.
    static constexpr float kStandOff = 200.0f;

    std::vector<State> states_;
    std::vector<std::size_t> nearby_;  // reused so a step allocates nothing
    std::map<std::int64_t, std::vector<std::size_t>> buckets_;
    Mm6Random random_{1};
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_MONSTER_AI_HPP
