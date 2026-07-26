#ifndef STARHAVEN_GAME_PLAYER_HPP
#define STARHAVEN_GAME_PLAYER_HPP

// The player's proportions and how they move. These are engine policy rather
// than decoded facts — the numbers are chosen to feel right, not read from the
// game — so they live apart from the format parsers.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/world/collision.hpp"
#include "core/world/map_session.hpp"

namespace starhaven::game {

// Player proportions, in MM6 world units. A terrain cell is 512 across, so a
// body a little under a third of a cell wide walks through doorways.
inline constexpr float kBodyRadius = 64.0f;
inline constexpr float kBodyHeight = 320.0f;
inline constexpr float kEyeHeight = 280.0f;
inline constexpr float kStepHeight = 96.0f;  // stairs this tall are walked up

// How fast the feet rise onto a ledge, in units per second. A 96-unit step
// takes about a sixth of a second, which reads as a step rather than a jump.
inline constexpr float kStepRate = 600.0f;

// The body is swept in pieces no longer than this, so a fast step cannot pass
// through a thin wall between two collision tests. Half a body radius means a
// wall is tested at least twice while the body crosses it.
inline constexpr float kSweepStep = kBodyRadius * 0.5f;
inline constexpr float kGravity = -2400.0f;  // units per second squared
inline constexpr float kMouseSensitivity = 0.0025f;
inline constexpr float kLookSpeed = 1.5f;  // radians per second, arrow keys

// A capture taken on the first frame shows the camera before gravity has
// settled it, which misrepresents where the player actually stands.
inline constexpr int kSettleFrames = 90;

// Where the player wants to go this frame, before collision.
struct MoveInput {
    bool forward = false, back = false, left = false, right = false;
    bool up = false, down = false;  // only used while flying
    float speed = 400.0f;
    float dt = 1.0f / 60.0f;
};

// Advance the camera. When `fly` is set the camera moves freely; otherwise the
// body is slid against the world and pulled down by gravity.
//
// `ground_extra` lets the outdoor walker add its heightfield, which is not part
// of the collision polygons.
template <typename GroundFn>
void step_player(render::Camera& camera, float& fall_speed, bool fly, const MoveInput& in,
                 const world::CollisionWorld& collision, GroundFn ground_extra) {
    const render::Vec3 flat = camera.forward_flat();
    const render::Vec3 side = camera.right();

    render::Vec3 wish = camera.position;
    const float step = in.speed * in.dt;
    if (in.forward)
        wish = wish + flat * step;
    if (in.back)
        wish = wish - flat * step;
    if (in.left)
        wish = wish - side * step;
    if (in.right)
        wish = wish + side * step;

    if (fly) {
        if (in.down)
            wish.y -= step;
        if (in.up)
            wish.y += step;
        camera.position = wish;
        return;
    }

    // Collide the body, not the eye: feet sit one eye height below the camera.
    const render::Vec3 feet_from{camera.position.x, camera.position.y - kEyeHeight,
                                 camera.position.z};
    const render::Vec3 feet_to{wish.x, wish.y - kEyeHeight, wish.z};

    // Sweep the body along the move instead of testing only its endpoints. A
    // single test lets a fast step cross a thin wall entirely between frames.
    auto sweep = [&](const render::Vec3& from, const render::Vec3& to) {
        const float dx = to.x - from.x;
        const float dz = to.z - from.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const int pieces = std::max(1, static_cast<int>(std::ceil(distance / kSweepStep)));
        render::Vec3 at = from;
        for (int i = 1; i <= pieces; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(pieces);
            at = collision.slide(at, {from.x + dx * t, at.y, from.z + dz * t}, kBodyRadius,
                                 kBodyHeight);
        }
        return at;
    };

    // Terrain is sampled rather than collided, so without a rise limit any
    // slope is climbable — walking into a cliff teleports the player up it.
    // A destination whose ground is more than a step above the current one is
    // refused, and the move is retried along each axis so a cliff can be
    // walked along rather than only away from.
    const float ground_here = ground_extra(feet_from.x, feet_from.z);
    const float rise_limit = std::max(feet_from.y, ground_here) + kStepHeight;
    auto walkable = [&](const render::Vec3& p) { return ground_extra(p.x, p.z) <= rise_limit; };

    render::Vec3 feet = sweep(feet_from, feet_to);
    if (!walkable(feet)) {
        const render::Vec3 along_x = sweep(feet_from, {feet_to.x, feet_from.y, feet_from.z});
        const render::Vec3 along_z = sweep(feet_from, {feet_from.x, feet_from.y, feet_to.z});
        if (walkable(along_x)) {
            feet = along_x;
        } else if (walkable(along_z)) {
            feet = along_z;
        } else {
            feet = feet_from;
        }
    }

    fall_speed += kGravity * in.dt;
    feet.y += fall_speed * in.dt;

    float ground = ground_extra(feet.x, feet.z);
    float from_polygons = 0.0f;
    if (collision.floor_below({feet.x, feet.y + kStepHeight, feet.z}, from_polygons)) {
        ground = std::max(ground, from_polygons);
    }
    if (feet.y <= ground) {
        // Rise onto the surface at a limited rate rather than snapping, so a
        // stair does not jolt the view. Falling is unaffected: gravity has
        // already moved the feet down this frame, and this only ever moves
        // them up.
        feet.y = std::min(ground, feet.y + kStepRate * in.dt);
        fall_speed = 0.0f;
    }
    camera.position = {feet.x, feet.y + kEyeHeight, feet.z};
}

// Parse "a,b,c" into up to `max_count` floats; returns how many were read.
inline int parse_floats(const std::string& text, float* out, int max_count) {
    int n = 0;
    std::size_t pos = 0;
    while (n < max_count && pos <= text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::string field = text.substr(pos, comma - pos);
        if (field.empty())
            break;
        out[n++] = std::strtof(field.c_str(), nullptr);
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return n;
}

// Locate Games.lod under the configured installation.
inline std::filesystem::path resolve_games_lod() {
    namespace fs = std::filesystem;
    if (auto install = platform::install_from_env()) {
        fs::path p = *install / "data" / "Games.lod";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "data/Games.lod";
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_PLAYER_HPP
