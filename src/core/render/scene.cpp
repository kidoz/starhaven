#include "core/render/scene.hpp"

#include <array>
#include <fstream>
#include <string>

namespace starhaven::render {

void SceneRenderer::begin(const Camera& camera, Color clear_color) {
    camera_ = camera;
    view_ = camera.view();
    projection_ = camera.projection(static_cast<float>(width_) / static_cast<float>(height_));
    view_projection_ = projection_ * view_;
    billboard_right_ = camera.right();

    framebuffer_.clear(clear_color);
    framebuffer_.clear_depth(1.0f);
}

bool SceneRenderer::project(const Mat4& transform, Vec3 point, float r, float g, float b, Vec2 uv,
                            ScreenVertex& out) const {
    const Vec4 clip = transform * Vec4{point.x, point.y, point.z, 1.0f};
    if (clip.w <= 0.0001f) {
        return false;  // behind or through the camera
    }
    const float inv_w = 1.0f / clip.w;
    out.x = (clip.x * inv_w * 0.5f + 0.5f) * static_cast<float>(width_);
    out.y = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * static_cast<float>(height_);
    out.z = clip.z * inv_w;
    out.r = r;
    out.g = g;
    out.b = b;
    out.u = uv.u;
    out.v = uv.v;
    // The rasterizer needs 1/w for perspective-correct interpolation, and this
    // is the only place it is known.
    out.inv_w = inv_w;
    return true;
}

bool SceneRenderer::project_point(Vec3 world, ScreenVertex& out) const {
    return project(view_projection_, world, 1.0f, 1.0f, 1.0f, {0.0f, 0.0f}, out);
}

void SceneRenderer::draw_triangle(std::span<const Vec3, 3> world, std::span<const Vec2, 3> uv,
                                  float shade, const Texture& texture, WrapMode wrap,
                                  bool cull_backfaces) {
    // Clip in view space, where u and v are still linear, then project. Doing
    // it the other way round would interpolate texture coordinates across the
    // near plane incorrectly.
    std::array<ViewVertex, 3> view_space{};
    for (std::size_t k = 0; k < 3; ++k) {
        const Vec4 vp = view_ * Vec4{world[k].x, world[k].y, world[k].z, 1};
        view_space[k] = {vp.x, vp.y, vp.z, shade, shade, shade, uv[k].u, uv[k].v};
    }
    std::vector<ViewVertex> clipped;
    clip_near(view_space.data(), /*near_z*/ -1.0f, clipped);

    for (std::size_t t = 0; t + 2 < clipped.size(); t += 3) {
        std::array<ScreenVertex, 3> screen{};
        bool ok = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const ViewVertex& v = clipped[t + k];
            // `clipped` is already in view space, so only the projection may be
            // applied here; using the combined matrix would transform twice.
            if (!project(projection_, {v.x, v.y, v.z}, v.r, v.g, v.b, {v.u, v.v}, screen[k])) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        if (texture.empty()) {
            framebuffer_.draw_triangle(screen[0], screen[1], screen[2], cull_backfaces);
        } else {
            framebuffer_.draw_triangle_textured(screen[0], screen[1], screen[2], texture, wrap,
                                                cull_backfaces);
        }
    }
}

void SceneRenderer::draw_billboard(Vec3 base, float width, float height, const Texture& texture,
                                   float shade) {
    if (texture.empty() || width <= 0.0f || height <= 0.0f) {
        return;
    }
    const Vec3 half = billboard_right_ * (width * 0.5f);
    const Vec3 bl = base - half;
    const Vec3 br = base + half;
    const Vec3 tr{br.x, br.y + height, br.z};
    const Vec3 tl{bl.x, bl.y + height, bl.z};

    // Two triangles, with the texture spanning the quad exactly once.
    const std::array<Vec3, 3> first = {tl, bl, br};
    const std::array<Vec2, 3> first_uv = {Vec2{0, 0}, Vec2{0, 1}, Vec2{1, 1}};
    draw_triangle(first, first_uv, shade, texture, WrapMode::Clamp, false);

    const std::array<Vec3, 3> second = {tl, br, tr};
    const std::array<Vec2, 3> second_uv = {Vec2{0, 0}, Vec2{1, 1}, Vec2{1, 0}};
    draw_triangle(second, second_uv, shade, texture, WrapMode::Clamp, false);
}

bool write_ppm(const std::string& path, const Framebuffer& fb) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << fb.width() << " " << fb.height() << "\n255\n";
    const auto pixels = fb.color();
    const std::size_t count =
        static_cast<std::size_t>(fb.width()) * static_cast<std::size_t>(fb.height());
    for (std::size_t i = 0; i < count; ++i) {
        out.put(static_cast<char>(pixels[i * 4 + 0]));
        out.put(static_cast<char>(pixels[i * 4 + 1]));
        out.put(static_cast<char>(pixels[i * 4 + 2]));
    }
    return static_cast<bool>(out);
}

}  // namespace starhaven::render
