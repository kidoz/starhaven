#ifndef STARHAVEN_GAME_INSPECT_HPP
#define STARHAVEN_GAME_INSPECT_HPP

// Looking at something and being told what it is. The monster and item tables
// are decoded and typed (docs/formats/text-tables.md); this turns a row of
// either into lines of text for a panel.

#include <algorithm>
#include <string>
#include <vector>

#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/spell_stats.hpp"
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

// How far apart to sample the map's tile index along the view ray. Half the
// radius a tile's list covers, so nothing between the eye and the range can
// fall between two samples.
inline constexpr float kTileIndexStep = 512.0f;

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

// The monster table writes a spell as "Name,Mastery,Skill" — sometimes several
// separated by semicolons. Only the names are wanted here, and only those the
// spell table knows.
inline std::string spell_names(const data::SpellStatsTable& spells, std::string_view field) {
    std::string out;
    std::size_t start = 0;
    while (start <= field.size()) {
        const std::size_t end = std::min(field.find(';', start), field.size());
        std::string_view one = field.substr(start, end - start);
        const std::size_t comma = one.find(',');
        if (comma != std::string_view::npos) {
            one = one.substr(0, comma);
        }
        one = data::trim(one);
        if (!one.empty() && spells.find(one) != nullptr) {
            if (!out.empty()) {
                out += ", ";
            }
            out += std::string(one);
        }
        start = end + 1;
    }
    return out;
}

// Describe a monster row the way a player would want it: what it is, how hard
// it hits, and what it shrugs off.
inline Inspected describe(const data::MonsterStatsEntry& m, const data::SpellStatsTable& spells) {
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
        // Prefer the spell table's names; fall back to the raw field when it
        // recognises none, so nothing is silently dropped.
        const std::string named = spell_names(spells, m.spells);
        out.lines.push_back("casts " + (named.empty() ? m.spells : named));
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

// And one of the map's decorations: a tree, a rock, a campfire. The global
// table is what knows anything about it beyond its name.
inline Inspected describe(const world::SessionDecoration& d, const world::DecorationTable& types) {
    Inspected out;
    out.title = d.name;
    if (const auto* type = types.find(d.name); type != nullptr) {
        if (!type->group.empty()) {
            out.lines.push_back(type->group);
        }
        if (type->sound_id != 0) {
            out.lines.push_back("makes a sound");
        }
    }
    return out;
}

// Which face with an event on it the party is looking at, and how far away it
// is. Only faces carrying an event id are considered — a few dozen per map —
// so this walks them rather than the level's thousands.
struct AimedFace {
    std::size_t index = 0;
    std::uint16_t event_id = 0;
    float distance = 0.0f;

    [[nodiscard]] bool found() const noexcept { return event_id != 0; }
};

// How far the party can read a sign or reach a door. `inferred`
inline constexpr float kUseRange = 512.0f;

[[nodiscard]] inline AimedFace aimed_face(const world::MapSession& session, const render::Vec3& eye,
                                          const render::Vec3& forward, float range = kUseRange) {
    AimedFace out;
    float nearest = range;

    // Outdoors the event sits on a model's facet rather than a level face.
    if (session.outdoor()) {
        for (const auto& mesh : session.meshes) {
            for (const auto& facet : mesh.facets) {
                if (facet.event_id == 0 || facet.vertex_count < 3) {
                    continue;
                }
                render::Vec3 centre{0, 0, 0};
                for (std::size_t k = 0; k < facet.vertex_count; ++k) {
                    const auto id = facet.vertex_ids[k];
                    if (id >= mesh.vertices.size()) {
                        centre = {0, 0, 0};
                        break;
                    }
                    const auto& p = mesh.vertices[id];
                    const render::Vec3 at = world::to_render_space(p.x, p.y, p.z);
                    centre.x += at.x;
                    centre.y += at.y;
                    centre.z += at.z;
                }
                const auto count = static_cast<float>(facet.vertex_count);
                centre = {centre.x / count, centre.y / count, centre.z / count};

                float distance = 0.0f;
                const float aim = detail::aim_score(eye, forward, centre, distance);
                if (aim < kInspectAim || distance > nearest) {
                    continue;
                }
                nearest = distance;
                out.event_id = facet.event_id;
                out.distance = distance;
            }
        }
        return out;
    }
    for (const auto& extra : session.blv.face_extras) {
        if (extra.event_id == 0 || extra.face_index >= session.blv.faces.size()) {
            continue;
        }
        const auto& face = session.blv.faces[extra.face_index];
        if (face.vertex_ids.empty()) {
            continue;
        }
        // The face's own centre is enough: a door is small and the party has
        // to be within arm's reach of it anyway.
        render::Vec3 centre{0, 0, 0};
        for (const std::uint16_t v : face.vertex_ids) {
            if (v >= session.blv.vertices.size()) {
                centre = {0, 0, 0};
                break;
            }
            const auto& p = session.blv.vertices[v];
            const render::Vec3 at = world::to_render_space(p.x, p.y, p.z);
            centre.x += at.x;
            centre.y += at.y;
            centre.z += at.z;
        }
        const auto count = static_cast<float>(face.vertex_ids.size());
        centre = {centre.x / count, centre.y / count, centre.z / count};

        float distance = 0.0f;
        const float aim = detail::aim_score(eye, forward, centre, distance);
        if (aim < kInspectAim || distance > nearest) {
            continue;
        }
        nearest = distance;
        out.index = extra.face_index;
        out.event_id = extra.event_id;
        out.distance = distance;
    }
    return out;
}

// What a face with an event on it calls itself, from its script.
[[nodiscard]] inline std::string face_name(const world::MapSession& session,
                                           std::uint16_t event_id) {
    const int index = session.script.string_of(event_id, world::kOpcodeName);
    return index < 0 ? std::string{}
                     : data::cp1252_to_utf8(
                           std::string(session.script_strings.at(static_cast<std::size_t>(index))));
}

// And what it says when the party uses it.
[[nodiscard]] inline std::string face_message(const world::MapSession& session,
                                              std::uint16_t event_id) {
    for (const std::uint8_t opcode : {world::kOpcodeMessage, world::kOpcodeLongMessage}) {
        const int index = session.script.string_of(event_id, opcode);
        if (index >= 0) {
            return data::cp1252_to_utf8(
                std::string(session.script_strings.at(static_cast<std::size_t>(index))));
        }
    }
    return {};
}

// What the player is looking at on this map, or nothing. Ties are broken by
// how directly the thing is being looked at, not by distance, so a monster
// behind a nearer one can still be picked by aiming past it.
//
// `visible` decides whether a point can actually be seen. The caller supplies
// it because the test belongs to the renderer — this layer has no framebuffer
// — and a caller with no way to answer can pass one that always says yes.
template <typename VisibleFn>
inline Inspected inspect(const world::MapSession& session, const data::MonsterStatsTable& monsters,
                         const data::ItemStatsTable& items, const data::SpellStatsTable& spells,
                         const render::Vec3& eye, const render::Vec3& forward, VisibleFn visible) {
    float best = kInspectAim;
    Inspected found;

    for (const auto& a : session.actors) {
        float distance = 0.0f;
        const float score = detail::aim_score(eye, forward, a.position, distance);
        if (score <= best || distance > kInspectRange || !visible(a.position)) {
            continue;
        }
        if (a.monster_id <= 0 ||
            static_cast<std::size_t>(a.monster_id) > monsters.entries().size()) {
            continue;
        }
        best = score;
        found = describe(monsters.entries()[static_cast<std::size_t>(a.monster_id) - 1], spells);
    }

    // Decorations are not searched: the outdoor maps ship a list of what
    // stands near each terrain tile, so the view ray is walked through that
    // index instead. A tile's neighbourhood is 1024 units across, so stepping
    // by half that cannot skip one.
    std::vector<std::size_t> candidates;
    for (float t = 0.0f; t <= kInspectRange; t += kTileIndexStep) {
        for (const std::size_t id :
             session.decorations_near(eye.x + forward.x * t, eye.z + forward.z * t)) {
            if (std::find(candidates.begin(), candidates.end(), id) == candidates.end()) {
                candidates.push_back(id);
            }
        }
    }
    for (const std::size_t id : candidates) {
        const auto& d = session.decorations[id];
        float distance = 0.0f;
        const float score = detail::aim_score(eye, forward, d.position, distance);
        if (score <= best || distance > kInspectRange || !visible(d.position)) {
            continue;
        }
        best = score;
        found = describe(d, session.decoration_types);
    }

    for (const auto& o : session.objects) {
        float distance = 0.0f;
        const float score = detail::aim_score(eye, forward, o.position, distance);
        if (score <= best || distance > kInspectRange || o.item_id <= 0 || !visible(o.position)) {
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

// Everything is visible. For callers with nothing to test against, and for
// tests that are not about occlusion.
struct AlwaysVisible {
    bool operator()(const render::Vec3&) const noexcept { return true; }
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INSPECT_HPP
