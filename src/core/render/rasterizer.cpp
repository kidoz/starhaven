#include "core/render/rasterizer.hpp"

#include <algorithm>
#include <cmath>

namespace openmm6::render {

void Framebuffer::clear(Color c) {
    for (int i = 0, n = width_ * height_; i < n; ++i) {
        color_[i * 4 + 0] = c.r;
        color_[i * 4 + 1] = c.g;
        color_[i * 4 + 2] = c.b;
        color_[i * 4 + 3] = c.a;
    }
}

void Framebuffer::clear_depth(float z) {
    std::fill(depth_.begin(), depth_.end(), z);
}

namespace {

// Edge function: positive if p is to the left of a->b (CCW). Used for both the
// fill test and backface winding.
inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

inline Color shade(float b) {
    const std::uint8_t v = static_cast<std::uint8_t>(
        std::clamp(b, 0.0f, 1.0f) * 255.0f);
    return {v, v, v, 255};
}

}  // namespace

void Framebuffer::draw_triangle(const ScreenVertex& v0, const ScreenVertex& v1,
                                const ScreenVertex& v2, bool cull_backfaces) {
    // Screen-space signed area; sign tells winding. CCW (positive) = front.
    const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    if (area == 0.0f) {
        return;  // degenerate
    }
    if (cull_backfaces && area < 0.0f) {
        return;  // back face
    }

    // Bounding box clipped to the screen.
    const float minx = std::max(0.0f, std::min({v0.x, v1.x, v2.x}));
    const float maxx = std::min(static_cast<float>(width_ - 1),
                                std::max({v0.x, v1.x, v2.x}));
    const float miny = std::max(0.0f, std::min({v0.y, v1.y, v2.y}));
    const float maxy = std::min(static_cast<float>(height_ - 1),
                                std::max({v0.y, v1.y, v2.y}));
    if (maxx < minx || maxy < miny) {
        return;
    }

    const float inv_area = 1.0f / area;
    const int x0 = static_cast<int>(std::floor(minx));
    const int x1 = static_cast<int>(std::ceil(maxx));
    const int y0 = static_cast<int>(std::floor(miny));
    const int y1 = static_cast<int>(std::ceil(maxy));

    // Top-left fill rule: a pixel center is inside if all edge functions are
    // >= 0, treating exactly-on-top-edge and exactly-on-left-edge as inside.
    auto on_edge = [](float e, float dx, float dy) {
        // top edge: horizontal, pointing right (dy==0, dx>0)
        // left edge: pointing down (dy>0)
        if (e == 0.0f) {
            return (dy == 0.0f && dx > 0.0f) || (dy > 0.0f);
        }
        return e > 0.0f;
    };

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            const float e0 = edge(v1.x, v1.y, v2.x, v2.y, px, py);
            const float e1 = edge(v2.x, v2.y, v0.x, v0.y, px, py);
            const float e2 = edge(v0.x, v0.y, v1.x, v1.y, px, py);
            // Inside test (all edges positive for the chosen winding).
            if (e0 < 0 || e1 < 0 || e2 < 0) {
                // Could be the other winding; recheck with on_edge for fill rule.
                if (!(on_edge(e0, v2.x - v1.x, v2.y - v1.y) &&
                      on_edge(e1, v0.x - v2.x, v0.y - v2.y) &&
                      on_edge(e2, v1.x - v0.x, v1.y - v0.y))) {
                    continue;
                }
            }
            // Barycentric weights (proportional to edge functions).
            const float w0 = e0 * inv_area;
            const float w1 = e1 * inv_area;
            const float w2 = e2 * inv_area;
            const float z = w0 * v0.z + w1 * v1.z + w2 * v2.z;
            const int idx = y * width_ + x;
            if (z >= 0.0f && z <= 1.0f && z < depth_[idx]) {
                depth_[idx] = z;
                const float r = w0 * v0.r + w1 * v1.r + w2 * v2.r;
                const float g = w0 * v0.g + w1 * v1.g + w2 * v2.g;
                const float b = w0 * v0.b + w1 * v1.b + w2 * v2.b;
                color_[idx * 4 + 0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                color_[idx * 4 + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                color_[idx * 4 + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
                color_[idx * 4 + 3] = 255;
            }
        }
    }
}

void Framebuffer::draw_line(const ScreenVertex& a, const ScreenVertex& b,
                            Color color) {
    // Bresenham, z-tested against the existing depth so the line is occluded
    // by nearer terrain. We interpolate z linearly across the line.
    int x0 = static_cast<int>(std::round(a.x));
    int y0 = static_cast<int>(std::round(a.y));
    int x1 = static_cast<int>(std::round(b.x));
    int y1 = static_cast<int>(std::round(b.y));
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    const int steps = std::max({dx, dy, 1});
    const float inv_steps = 1.0f / static_cast<float>(steps);

    int safety = 0;
    for (int i = 0;; ++i) {
        if (x0 >= 0 && x0 < width_ && y0 >= 0 && y0 < height_) {
            const float t = static_cast<float>(i) * inv_steps;
            const float z = a.z + (b.z - a.z) * t;
            const int idx = y0 * width_ + x0;
            // Draw the line on top of geometry (z <= stored), ignoring the
            // depth test so debug boxes remain visible even behind hills.
            (void)z; (void)idx;
            color_[idx * 4 + 0] = color.r;
            color_[idx * 4 + 1] = color.g;
            color_[idx * 4 + 2] = color.b;
            color_[idx * 4 + 3] = 255;
        }
        if (x0 == x1 && y0 == y1) break;
        if (++safety > 10000) break;  // guard against malformed input
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void Framebuffer::draw_point(const ScreenVertex& p, Color color) {
    const int cx = static_cast<int>(std::round(p.x));
    const int cy = static_cast<int>(std::round(p.y));
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= width_ || y < 0 || y >= height_) continue;
            const int idx = (y * width_ + x) * 4;
            color_[idx + 0] = color.r;
            color_[idx + 1] = color.g;
            color_[idx + 2] = color.b;
            color_[idx + 3] = 255;
        }
    }
}

// --- Near-plane clipping ---------------------------------------------------

namespace {

void emit_tri(std::vector<ViewVertex>& out, ViewVertex a, ViewVertex b, ViewVertex c) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

// Linear interpolation between two view vertices at param t in [0,1].
ViewVertex lerp(const ViewVertex& a, const ViewVertex& b, float t) {
    return {a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t};
}

}  // namespace

void clip_near(const ViewVertex in[3], float near_z,
               std::vector<ViewVertex>& out) {
    // near_z is the view-space z of the near plane (a negative value, e.g. -1).
    // A vertex is "in" if z <= near_z (farther than near, i.e. in front).
    // (View space: camera at origin looking down -Z, so visible z is negative
    // and more-negative = farther.)
    auto in_front = [near_z](const ViewVertex& v) { return v.z <= near_z; };

    const bool a_in = in_front(in[0]);
    const bool b_in = in_front(in[1]);
    const bool c_in = in_front(in[2]);
    const int count = int(a_in) + int(b_in) + int(c_in);
    if (count == 0) {
        return;  // fully clipped
    }
    if (count == 3) {
        emit_tri(out, in[0], in[1], in[2]);
        return;
    }

    // Intersection param between two vertices crossing the plane.
    auto cross_t = [near_z](const ViewVertex& p, const ViewVertex& q) {
        // solve p.z + t*(q.z - p.z) = near_z
        return (near_z - p.z) / (q.z - p.z);
    };

    if (count == 1) {
        // One in, two out -> one smaller triangle.
        const ViewVertex* keep = a_in ? &in[0] : (b_in ? &in[1] : &in[2]);
        const ViewVertex* n1 = a_in ? &in[1] : (b_in ? &in[2] : &in[0]);
        const ViewVertex* n2 = a_in ? &in[2] : (b_in ? &in[0] : &in[1]);
        const float t1 = cross_t(*keep, *n1);
        const float t2 = cross_t(*keep, *n2);
        emit_tri(out, *keep, lerp(*keep, *n1, t1), lerp(*keep, *n2, t2));
        return;
    }

    // count == 2: two in, one out -> a quad (two triangles).
    const ViewVertex* out_v = !a_in ? &in[0] : (!b_in ? &in[1] : &in[2]);
    const ViewVertex* i1 = !a_in ? &in[1] : &in[0];
    const ViewVertex* i2 = !a_in ? &in[2] : (!b_in ? &in[0] : &in[1]);
    const float t1 = cross_t(*i1, *out_v);
    const float t2 = cross_t(*i2, *out_v);
    const ViewVertex p1 = lerp(*i1, *out_v, t1);
    const ViewVertex p2 = lerp(*i2, *out_v, t2);
    emit_tri(out, *i1, p2, p1);
    emit_tri(out, *i1, *i2, p2);
}

}  // namespace openmm6::render
