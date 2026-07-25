#ifndef OPENMM6_CORE_RENDER_RASTERIZER_HPP
#define OPENMM6_CORE_RENDER_RASTERIZER_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "core/render/math3d.hpp"

namespace openmm6::render {

// An RGBA color (8-bit channels).
struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

// A renderable vertex after transform: screen-space x,y plus NDC z in [0,1]
// and an interpolated color channel (linear RGB 0..1, premultiplied by shading).
struct ScreenVertex {
    float x = 0;     // pixel x (may be off-screen)
    float y = 0;     // pixel y
    float z = 0;     // NDC depth in [0,1] (0 = near, 1 = far)
    float r = 1, g = 1, b = 1;  // linear RGB 0..1 (e.g. shaded surface color)
};

// A w×h framebuffer with an RGBA color buffer and a [0,1] z-buffer. y grows
// downward (row 0 at the top of the screen).
class Framebuffer {
public:
    Framebuffer(int width, int height)
        : width_(width), height_(height),
          color_(static_cast<std::size_t>(width) * height * 4, 0),
          depth_(static_cast<std::size_t>(width) * height, 1.0f) {}

    int width() const { return width_; }
    int height() const { return height_; }
    std::span<std::uint8_t> color() { return color_; }
    std::span<const std::uint8_t> color() const { return color_; }

    void clear(Color c);
    void clear_depth(float z = 1.0f);

    // Rasterize a filled, z-buffered triangle. Vertices may be in any order.
    // Backface culling is optional (pass true to skip CCW-back faces, where
    // "back" is determined by screen-space winding).
    void draw_triangle(const ScreenVertex& a, const ScreenVertex& b,
                       const ScreenVertex& c, bool cull_backfaces);

    // Draw a z-tested line (wireframe) in the given color. Used for debugging
    // overlays such as model bounding boxes.
    void draw_line(const ScreenVertex& a, const ScreenVertex& b, Color color);

    // Plot a single point (a small 3x3 block) in the given color. Used for
    // debug overlays such as model vertex clouds.
    void draw_point(const ScreenVertex& p, Color color);

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> color_;
    std::vector<float> depth_;
};

// Clip a triangle (in view space, pre-projection) against the near plane
// z = -near. Emits 0, 1, or 2 triangles. Each view vertex carries (x,y,z,r,g,b).
// This keeps the rasterizer from dividing by w<=0 for geometry behind/through
// the camera.
struct ViewVertex { float x, y, z, r, g, b; };
void clip_near(const ViewVertex in[3], float near_z,
               std::vector<ViewVertex>& out_tris);

}  // namespace openmm6::render

#endif  // OPENMM6_CORE_RENDER_RASTERIZER_HPP
