#ifndef STARHAVEN_GAME_LAUNCHES_HPP
#define STARHAVEN_GAME_LAUNCHES_HPP

// Sprites in flight: what a map script's launch opcode puts in the air.
//
// The record states the animation and two points; that the sprite flies from
// the first toward the second is `inferred` from the shipped pairs being
// axis-aligned runs down hallways. A launch with no second point states no
// target at all — this engine flies it at the party, the natural reading of
// a trap, and marks the choice its own. Speed is the engine's too: the
// record's middle byte may be one, but what scale it is on is `unknown`.
// Whether a bolt hurts is not in the record either; these fly and land, and
// nothing more. See docs/formats/map-events.md.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/render/math3d.hpp"
#include "core/world/map_script.hpp"
#include "core/world/map_session.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace starhaven::game {

// How fast a launched sprite flies, in MM6 units per second. Engine-own: a
// hall of Castle Darkmoor is a few thousand units, and this crosses it in a
// few heartbeats.
inline constexpr float kLaunchSpeed = 900.0f;

// How close counts as arrived, squared.
inline constexpr float kLaunchArrive = 32.0f;

// One sprite in the air.
struct ActiveLaunch {
    std::string animation;  // a sprite frame table group name
    render::Vec3 position;
    render::Vec3 target;
    bool arrived = false;
};

// Put a walked launch in the air. `party` is where an aimless one flies,
// in renderer axes. Returns nothing when the animation does not resolve.
[[nodiscard]] inline std::vector<ActiveLaunch> start_launches(
    const std::vector<world::MapLaunch>& launches, const world::SpriteFrameTable& frames,
    const render::Vec3& party) {
    std::vector<ActiveLaunch> out;
    for (const auto& l : launches) {
        const std::string_view name =
            frames.group_name_at(static_cast<std::size_t>(l.animation));
        if (name.empty()) {
            continue;
        }
        ActiveLaunch active;
        active.animation = std::string(name);
        active.position = world::to_render_space(l.from_x, l.from_y, l.from_z);
        active.target = l.aimless() ? party : world::to_render_space(l.to_x, l.to_y, l.to_z);
        out.push_back(std::move(active));
    }
    return out;
}

// Fly everything one frame further; what reaches its target is removed.
inline void advance_launches(std::vector<ActiveLaunch>& launches, float dt) {
    for (auto& l : launches) {
        const render::Vec3 gap{l.target.x - l.position.x, l.target.y - l.position.y,
                               l.target.z - l.position.z};
        const float distance = std::sqrt(gap.x * gap.x + gap.y * gap.y + gap.z * gap.z);
        const float step = kLaunchSpeed * dt;
        if (distance <= step || distance * distance <= kLaunchArrive * kLaunchArrive) {
            l.arrived = true;
            continue;
        }
        l.position.x += gap.x / distance * step;
        l.position.y += gap.y / distance * step;
        l.position.z += gap.z / distance * step;
    }
    std::erase_if(launches, [](const ActiveLaunch& l) { return l.arrived; });
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_LAUNCHES_HPP
