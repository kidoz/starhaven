#ifndef STARHAVEN_TOOLS_WALKER_COMMON_HPP
#define STARHAVEN_TOOLS_WALKER_COMMON_HPP

// Pieces both first-person walkers need: the player's proportions, the
// movement step, and the small argument helpers. Kept here rather than in the
// engine because these are viewer policy, not engine behaviour — the numbers
// are chosen to feel right, not read from the game.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/world/collision.hpp"

namespace starhaven::tools {

// Player proportions, in MM6 world units. A terrain cell is 512 across, so a
// body a little under a third of a cell wide walks through doorways.
inline constexpr float kBodyRadius = 64.0f;
inline constexpr float kBodyHeight = 320.0f;
inline constexpr float kEyeHeight = 280.0f;
inline constexpr float kStepHeight = 96.0f;   // stairs this tall are walked up
inline constexpr float kGravity = -2400.0f;   // units per second squared
inline constexpr float kMouseSensitivity = 0.0025f;
inline constexpr float kLookSpeed = 1.5f;     // radians per second, arrow keys

// A capture taken on the first frame shows the camera before gravity has
// settled it, which misrepresents where the player actually stands.
inline constexpr int kSettleFrames = 90;

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up.
inline render::Vec3 to_render_space(int x, int y, int z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

// Where the player wants to go this frame, before collision.
struct MoveInput {
    bool forward = false, back = false, left = false, right = false;
    bool up = false, down = false;   // only used while flying
    float speed = 400.0f;
    float dt = 1.0f / 60.0f;
};

// Advance the camera. When `fly` is set the camera moves freely; otherwise the
// body is slid against the world and pulled down by gravity.
//
// `ground_extra` lets the outdoor walker add its heightfield, which is not part
// of the collision polygons.
template <typename GroundFn>
void step_player(render::Camera& camera, float& fall_speed, bool fly,
                 const MoveInput& in, const world::CollisionWorld& collision,
                 GroundFn ground_extra) {
    const render::Vec3 flat = camera.forward_flat();
    const render::Vec3 side = camera.right();

    render::Vec3 wish = camera.position;
    const float step = in.speed * in.dt;
    if (in.forward) wish = wish + flat * step;
    if (in.back) wish = wish - flat * step;
    if (in.left) wish = wish - side * step;
    if (in.right) wish = wish + side * step;

    if (fly) {
        if (in.down) wish.y -= step;
        if (in.up) wish.y += step;
        camera.position = wish;
        return;
    }

    // Collide the body, not the eye: feet sit one eye height below the camera.
    const render::Vec3 feet_from{camera.position.x, camera.position.y - kEyeHeight,
                                 camera.position.z};
    const render::Vec3 feet_to{wish.x, wish.y - kEyeHeight, wish.z};
    render::Vec3 feet = collision.slide(feet_from, feet_to, kBodyRadius, kBodyHeight);

    fall_speed += kGravity * in.dt;
    feet.y += fall_speed * in.dt;

    float ground = ground_extra(feet.x, feet.z);
    float from_polygons = 0.0f;
    if (collision.floor_below({feet.x, feet.y + kStepHeight, feet.z},
                              from_polygons)) {
        ground = std::max(ground, from_polygons);
    }
    if (feet.y <= ground) {
        feet.y = ground;
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
        if (field.empty()) break;
        out[n++] = std::strtof(field.c_str(), nullptr);
        if (comma == std::string::npos) break;
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

}  // namespace starhaven::tools

#endif  // STARHAVEN_TOOLS_WALKER_COMMON_HPP
