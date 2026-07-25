#ifndef STARHAVEN_CORE_RENDER_SCENE_HPP
#define STARHAVEN_CORE_RENDER_SCENE_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "core/render/color.hpp"
#include "core/render/math3d.hpp"
#include "core/render/rasterizer.hpp"
#include "core/render/texture.hpp"

namespace starhaven::render {

// A first-person camera: a position and a look direction, plus the matrices a
// frame needs. Movement lives in the walkers, which own the collision rules.
struct Camera {
    Vec3 position{0, 0, 0};
    float yaw = 0.0f;     // radians; 0 looks down -Z
    float pitch = 0.0f;   // radians; positive looks up

    // Clamped so the camera cannot roll past vertical.
    static constexpr float kMaxPitch = 1.4f;

    [[nodiscard]] Vec3 forward() const { return camera_forward(yaw, pitch); }
    [[nodiscard]] Vec3 right() const { return camera_right(yaw); }
    // Forward with the vertical component removed, for walking.
    [[nodiscard]] Vec3 forward_flat() const {
        const Vec3 f = forward();
        return normalize(Vec3{f.x, 0, f.z});
    }

    [[nodiscard]] Mat4 view() const {
        return mat4_look_at(position, position + forward(), {0, 1, 0});
    }
    // Static in effect: the projection depends on the viewport, not on where
    // the camera is, but it lives here so a frame asks one object for both.
    [[nodiscard]] static Mat4 projection(float aspect, float fov_degrees = 60.0f,
                                         float near_plane = 1.0f,
                                         float far_plane = 20000.0f) {
        return mat4_perspective(radians(fov_degrees), aspect, near_plane, far_plane);
    }
};

// Draws world geometry into a framebuffer for one camera.
//
// Both walkers used to carry their own copy of the transform, near-clip and
// projection sequence; keeping it here means a change to the pipeline is made
// once and both get it.
class SceneRenderer {
public:
    SceneRenderer(int width, int height)
        : width_(width), height_(height), framebuffer_(width, height) {}

    // Clear the colour and depth buffers and fix the camera for this frame.
    void begin(const Camera& camera, Color clear_color);

    // Rasterize one flat-shaded world-space triangle. `shade` scales all three
    // colour channels, so a scalar Lambert term modulates the texture.
    //
    // Drawing with an empty texture falls back to a flat fill, which is what
    // untextured geometry needs in order to still read as solid.
    void draw_triangle(std::span<const Vec3, 3> world,
                       std::span<const Vec2, 3> uv, float shade,
                       const Texture& texture, WrapMode wrap, bool cull_backfaces);

    // Rasterize a camera-facing quad standing on `base`, `width` wide and
    // `height` tall. Fully transparent texels are skipped by the rasterizer,
    // which is what gives sprites their cut-out silhouette.
    void draw_billboard(Vec3 base, float width, float height,
                        const Texture& texture, float shade = 1.0f);

    // Project a world point for overlay drawing (lines, points). Returns false
    // when the point is behind the camera.
    [[nodiscard]] bool project_point(Vec3 world, ScreenVertex& out) const;

    [[nodiscard]] Framebuffer& framebuffer() noexcept { return framebuffer_; }
    [[nodiscard]] const Framebuffer& framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    [[nodiscard]] bool project(const Mat4& transform, Vec3 point, float r, float g,
                               float b, Vec2 uv, ScreenVertex& out) const;

    int width_;
    int height_;
    Framebuffer framebuffer_;
    Camera camera_{};
    Mat4 view_{};
    Mat4 projection_{};
    Mat4 view_projection_{};
    Vec3 billboard_right_{1, 0, 0};
};

// Write a framebuffer to a binary PPM. Used by the walkers' screenshot mode.
[[nodiscard]] bool write_ppm(const std::string& path, const Framebuffer& fb);

}  // namespace starhaven::render

#endif  // STARHAVEN_CORE_RENDER_SCENE_HPP
