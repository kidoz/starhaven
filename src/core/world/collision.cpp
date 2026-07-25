#include "core/world/collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace starhaven::world {

using render::Vec3;

namespace {

// Index of the largest component of the normal: the axis to drop when
// projecting the polygon to 2D.
[[nodiscard]] int dominant_axis(Vec3 n) {
    const float ax = std::fabs(n.x);
    const float ay = std::fabs(n.y);
    const float az = std::fabs(n.z);
    if (ax >= ay && ax >= az) return 0;
    return (ay >= az) ? 1 : 2;
}

// Drop the dominant axis, leaving a 2D point the winding test can use.
void project(Vec3 p, int axis, float& u, float& v) {
    switch (axis) {
        case 0: u = p.y; v = p.z; break;
        case 1: u = p.x; v = p.z; break;
        default: u = p.x; v = p.y; break;
    }
}

}  // namespace

bool point_in_polygon(const CollisionPolygon& polygon, Vec3 point) {
    const std::size_t n = polygon.vertices.size();
    if (n < 3) {
        return false;
    }
    const int axis = dominant_axis(polygon.normal);

    float px = 0;
    float py = 0;
    project(point, axis, px, py);

    // Crossing-number test. Polygons here are convex, but the crossing test
    // costs the same and does not care.
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        float ix = 0;
        float iy = 0;
        float jx = 0;
        float jy = 0;
        project(polygon.vertices[i], axis, ix, iy);
        project(polygon.vertices[j], axis, jx, jy);
        const bool straddles = (iy > py) != (jy > py);
        if (straddles) {
            const float t = (py - iy) / (jy - iy);
            if (px < ix + t * (jx - ix)) {
                inside = !inside;
            }
        }
    }
    return inside;
}

void CollisionWorld::add_polygon(std::span<const Vec3> vertices, Vec3 normal) {
    if (vertices.size() < 3) {
        return;
    }
    const float len = std::sqrt(normal.x*normal.x + normal.y*normal.y +
                                normal.z*normal.z);
    if (!(len > 1e-6f)) {
        return;  // a degenerate normal cannot define a plane
    }

    CollisionPolygon poly;
    poly.vertices.assign(vertices.begin(), vertices.end());
    poly.normal = {normal.x / len, normal.y / len, normal.z / len};
    // Anchor the plane on the first vertex.
    const Vec3 a = poly.vertices.front();
    poly.distance = -(poly.normal.x*a.x + poly.normal.y*a.y + poly.normal.z*a.z);
    polygons_.push_back(std::move(poly));
}

bool CollisionWorld::floor_below(Vec3 from, float& out_y) const {
    bool found = false;
    float best = 0.0f;

    for (const auto& poly : polygons_) {
        if (poly.normal.y < kFloorNormalY) {
            continue;  // too steep to stand on
        }
        // Where a vertical line through `from` meets the plane.
        //   normal . (from.x, y, from.z) + distance == 0
        const float denom = poly.normal.y;
        if (std::fabs(denom) < 1e-6f) {
            continue;
        }
        const float y = -(poly.normal.x * from.x + poly.normal.z * from.z +
                          poly.distance) / denom;
        // A small tolerance keeps the surface the player is already standing
        // on from being rejected by rounding.
        if (y > from.y + 1.0f) {
            continue;
        }
        if (!point_in_polygon(poly, {from.x, y, from.z})) {
            continue;
        }
        if (!found || y > best) {
            found = true;
            best = y;
        }
    }
    if (found) {
        out_y = best;
    }
    return found;
}

Vec3 CollisionWorld::slide(Vec3 from, Vec3 to, float radius, float height) const {
    Vec3 pos = to;

    // Test at knee and shoulder rather than at a single point, so a low sill
    // and a high beam both register.
    const std::array<float, 2> samples = {height * 0.25f, height * 0.75f};

    // A few passes let a corner push the player out of both walls.
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (const auto& poly : polygons_) {
            if (poly.normal.y >= kFloorNormalY) {
                continue;  // floors are handled by gravity, not by pushing
            }
            for (const float dy : samples) {
                const Vec3 probe{pos.x, pos.y + dy, pos.z};
                const float d = poly.normal.x*probe.x + poly.normal.y*probe.y +
                                poly.normal.z*probe.z + poly.distance;
                if (d >= radius) {
                    continue;  // still clear of this wall
                }
                // Faces are one-sided: only block someone who was in front of
                // the wall to begin with. Testing the start position is also
                // what stops a fast step from tunnelling clean through, since
                // `d` alone cannot tell "just short of" from "already past".
                const Vec3 start{from.x, from.y + dy, from.z};
                const float d_from = poly.normal.x*start.x +
                                     poly.normal.y*start.y +
                                     poly.normal.z*start.z + poly.distance;
                if (d_from < 0.0f) {
                    continue;
                }
                // The nearest point on the plane must be within the polygon,
                // otherwise the wall does not extend to where the player is.
                const Vec3 nearest{probe.x - poly.normal.x * d,
                                   probe.y - poly.normal.y * d,
                                   probe.z - poly.normal.z * d};
                if (!point_in_polygon(poly, nearest)) {
                    continue;
                }
                const float push = radius - d;
                pos.x += poly.normal.x * push;
                pos.y += poly.normal.y * push;
                pos.z += poly.normal.z * push;
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
    }
    return pos;
}

}  // namespace starhaven::world
