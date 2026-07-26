#ifndef STARHAVEN_GAME_INSPECT_HPP
#define STARHAVEN_GAME_INSPECT_HPP

// Looking at something and being told what it is. The monster and item tables
// are decoded and typed (docs/formats/text-tables.md); this turns a row of
// either into lines of text for a panel.

#include <string>
#include <vector>

#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/render/math3d.hpp"
#include "core/world/map_session.hpp"

namespace starhaven::game {

// What the player is looking at, if anything.
struct Inspected {
    std::string title;
    std::vector<std::string> lines;

    [[nodiscard]] bool empty() const noexcept { return title.empty(); }
};

// How closely the player has to be looking at something for it to count, as
// the cosine of the angle off the view axis. About 12 degrees.
inline constexpr float kInspectAim = 0.978f;

// And how far. Beyond this a monster is a smudge, not a subject.
inline constexpr float kInspectRange = 3000.0f;

namespace detail {

inline float aim_score(const render::Vec3& eye, const render::Vec3& forward, const render::Vec3& at,
                       float& out_distance) {
    const render::Vec3 d{at.x - eye.x, at.y - eye.y, at.z - eye.z};
    const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    out_distance = distance;
    if (distance <= 0.001f) {
        return -1.0f;
    }
    return (d.x * forward.x + d.y * forward.y + d.z * forward.z) / distance;
}

inline std::string resistance_text(const data::MonsterStatsEntry& m, const char* label,
                                   data::Resistance r) {
    const int v = m.resistance(r);
    return std::string(label) + " " + (v == data::kResistanceImmune ? "immune" : std::to_string(v));
}

}  // namespace detail

// Describe a monster row the way a player would want it: what it is, how hard
// it hits, and what it shrugs off.
inline Inspected describe(const data::MonsterStatsEntry& m) {
    Inspected out;
    out.title = m.name;
    out.lines.push_back("level " + std::to_string(m.level) + ", " + std::to_string(m.hit_points) +
                        " hit points, armour " + std::to_string(m.armor_class));
    out.lines.push_back(std::to_string(m.experience) + " experience");

    for (const auto& a : m.attacks) {
        if (a.type.empty() || a.type == "0") {
            continue;
        }
        std::string line = "attack: " + a.type + " " + a.damage;
        if (!a.missile.empty() && a.missile != "0") {
            line += " (" + a.missile + ")";
        }
        out.lines.push_back(line);
    }
    if (!m.spells.empty() && m.spells != "0") {
        out.lines.push_back("casts " + m.spells);
    }

    out.lines.push_back(detail::resistance_text(m, "fire", data::Resistance::Fire) + ", " +
                        detail::resistance_text(m, "cold", data::Resistance::Cold) + ", " +
                        detail::resistance_text(m, "magic", data::Resistance::Magic));
    return out;
}

// And an item row.
inline Inspected describe(const data::ItemStatsEntry& item) {
    Inspected out;
    out.title = item.name;
    if (!item.equip_stat.empty() && item.equip_stat != "0") {
        out.lines.push_back(item.equip_stat +
                            (item.skill_group.empty() ? "" : " (" + item.skill_group + ")"));
    }
    if (!item.modifier_1.empty() && item.modifier_1 != "0") {
        out.lines.push_back("damage " + item.modifier_1);
    }
    out.lines.push_back(std::to_string(item.value) + " gold");
    return out;
}

// What the player is looking at on this map, or nothing. Ties are broken by
// how directly the thing is being looked at, not by distance, so a monster
// behind a nearer one can still be picked by aiming past it.
inline Inspected inspect(const world::MapSession& session, const data::MonsterStatsTable& monsters,
                         const data::ItemStatsTable& items, const render::Vec3& eye,
                         const render::Vec3& forward) {
    float best = kInspectAim;
    Inspected found;

    for (const auto& a : session.actors) {
        float distance = 0.0f;
        const float score = detail::aim_score(eye, forward, a.position, distance);
        if (score <= best || distance > kInspectRange) {
            continue;
        }
        if (a.monster_id <= 0 ||
            static_cast<std::size_t>(a.monster_id) > monsters.entries().size()) {
            continue;
        }
        best = score;
        found = describe(monsters.entries()[static_cast<std::size_t>(a.monster_id) - 1]);
    }

    for (const auto& o : session.objects) {
        float distance = 0.0f;
        const float score = detail::aim_score(eye, forward, o.position, distance);
        if (score <= best || distance > kInspectRange || o.item_id <= 0) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(o.item_id));
        if (row == nullptr) {
            continue;
        }
        best = score;
        found = describe(*row);
    }
    return found;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INSPECT_HPP
