#include "core/world/collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/render/terrain_mesh.hpp"

namespace starhaven::world {

using render::Vec3;

namespace {

[[nodiscard]] bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Sweeps use double intermediates: finite float endpoints can have a
// difference or squared distance that overflows float.
struct SweepVector {
    double x = 0, y = 0, z = 0;
};

[[nodiscard]] SweepVector precise(Vec3 v) {
    return {v.x, v.y, v.z};
}
[[nodiscard]] Vec3 rounded(SweepVector v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
[[nodiscard]] SweepVector operator+(SweepVector a, SweepVector b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] SweepVector operator-(SweepVector a, SweepVector b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] SweepVector operator*(SweepVector a, double scale) {
    return {a.x * scale, a.y * scale, a.z * scale};
}
[[nodiscard]] double dot(SweepVector a, SweepVector b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] SweepVector closest_on_edge(SweepVector point, SweepVector a, SweepVector b) {
    const auto edge = b - a;
    const double length_squared = dot(edge, edge);
    if (length_squared == 0.0)
        return a;
    return a + edge * std::clamp(dot(point - a, edge) / length_squared, 0.0, 1.0);
}

// Crossing-number membership in double precision. Boundary contacts are also
// tested against the edge capsules, so they do not depend on winding parity.
[[nodiscard]] bool sweep_inside(const CollisionPolygon& polygon, SweepVector point) {
    const auto n = polygon.normal;
    int axis = std::abs(n.y) >= std::abs(n.z) ? 1 : 2;
    if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z))
        axis = 0;
    const auto coordinates = [axis](SweepVector v) -> std::array<double, 2> {
        if (axis == 0)
            return {v.y, v.z};
        if (axis == 1)
            return {v.x, v.z};
        return {v.x, v.y};
    };
    const auto p = coordinates(point);
    bool inside = false;
    for (std::size_t i = 0, j = polygon.vertices.size() - 1; i < polygon.vertices.size(); j = i++) {
        const auto a = coordinates(precise(polygon.vertices[i]));
        const auto b = coordinates(precise(polygon.vertices[j]));
        if ((a[1] > p[1]) != (b[1] > p[1]) &&
            p[0] < a[0] + (p[1] - a[1]) / (b[1] - a[1]) * (b[0] - a[0]))
            inside = !inside;
    }
    return inside;
}

// Entry of a moving point into a sphere centered at zero. This closest-line
// form avoids subtracting large, nearly equal quadratic discriminant terms.
[[nodiscard]] std::optional<double> sphere_entry(SweepVector offset, SweepVector movement,
                                                 double radius) {
    const double length = std::sqrt(dot(movement, movement));
    if (length == 0.0)
        return std::nullopt;
    const auto direction = movement * (1.0 / length);
    const double along = dot(offset, direction);
    const auto perpendicular = offset - direction * along;
    const double distance_squared = dot(perpendicular, perpendicular);
    const double radius_squared = radius * radius;
    const double remainder = radius_squared - distance_squared;
    // The line projection has an absolute rounding error relative to the
    // starting offset. Include its square for exact zero-radius vertex hits.
    const double projection_error =
        32.0 * std::numeric_limits<double>::epsilon() * std::sqrt(dot(offset, offset));
    const double tolerance =
        projection_error * projection_error +
        32.0 * std::numeric_limits<double>::epsilon() * std::max(radius_squared, distance_squared);
    if (remainder < -tolerance)
        return std::nullopt;
    const double fraction = (-along - std::sqrt(std::max(0.0, remainder))) / length;
    if (fraction < 0.0 || fraction > 1.0)
        return std::nullopt;
    return fraction;
}

struct PreciseSweepHit {
    double fraction;
    SweepVector normal;
    double penetration = 0.0;
};

[[nodiscard]] std::optional<PreciseSweepHit> sweep_polygon(const CollisionPolygon& polygon,
                                                           SweepVector from, SweepVector movement,
                                                           double radius) {
    auto normal = precise(polygon.normal);
    normal = normal * (1.0 / std::sqrt(dot(normal, normal)));
    const auto first = precise(polygon.vertices.front());
    const double distance = dot(from - first, normal);
    const double normal_motion = dot(movement, normal);
    const auto fallback_normal = normal * (normal_motion > 0.0 ? -1.0 : 1.0);
    const auto outward = [fallback_normal, radius](SweepVector delta) {
        const double length = std::sqrt(dot(delta, delta));
        return radius > 0.0 && length > 0.0 ? delta * (1.0 / length) : fallback_normal;
    };

    // Find the nearest polygon point first. Overlap must be handled before
    // testing moving entry roots, including starts inside an edge capsule.
    auto closest = first;
    double nearest_squared = dot(from - first, from - first);
    const auto projection = from - normal * distance;
    if (sweep_inside(polygon, projection)) {
        closest = projection;
        nearest_squared = distance * distance;
    }
    for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
        const auto a = precise(polygon.vertices[i]);
        const auto b = precise(polygon.vertices[(i + 1) % polygon.vertices.size()]);
        const auto point = closest_on_edge(from, a, b);
        const double squared = dot(from - point, from - point);
        if (squared < nearest_squared) {
            closest = point;
            nearest_squared = squared;
        }
    }
    if (nearest_squared <= radius * radius)
        return PreciseSweepHit{0.0, outward(from - closest), radius - std::sqrt(nearest_squared)};

    std::optional<PreciseSweepHit> hit;
    const auto consider = [&](double fraction, SweepVector contact_normal) {
        if (fraction >= 0.0 && fraction <= 1.0 && (!hit || fraction < hit->fraction))
            hit = PreciseSweepHit{fraction, contact_normal};
    };
    // The planar patches at +radius and -radius form the flat parts of the
    // sphere-expanded polygon. Only the approaching side is an entry.
    if (normal_motion != 0.0) {
        const double side = normal_motion < 0.0 ? 1.0 : -1.0;
        const double fraction = (side * radius - distance) / normal_motion;
        if (fraction >= 0.0 && fraction <= 1.0 &&
            sweep_inside(polygon, from + movement * fraction - normal * (side * radius)))
            consider(fraction, normal * side);
    }
    for (std::size_t i = 0; i < polygon.vertices.size(); ++i) {
        const auto a = precise(polygon.vertices[i]);
        const auto b = precise(polygon.vertices[(i + 1) % polygon.vertices.size()]);
        const auto edge = b - a;
        const double edge_squared = dot(edge, edge);
        const auto offset = from - a;
        // An edge capsule consists of its infinite cylinder restricted to
        // the segment and the endpoint spheres (one per polygon vertex).
        if (edge_squared > 0.0) {
            const auto perpendicular_start = offset - edge * (dot(offset, edge) / edge_squared);
            const auto perpendicular_move = movement - edge * (dot(movement, edge) / edge_squared);
            if (const auto fraction =
                    sphere_entry(perpendicular_start, perpendicular_move, radius)) {
                const auto center = from + movement * *fraction;
                const double along = dot(center - a, edge) / edge_squared;
                if (along >= 0.0 && along <= 1.0)
                    consider(*fraction, outward(center - (a + edge * along)));
            }
        }
        if (const auto fraction = sphere_entry(offset, movement, radius))
            consider(*fraction, outward(from + movement * *fraction - a));
    }
    return hit;
}

// The renderer splits a cell into (a,b,d) and (b,c,d). Reconstruct only
// candidate cells instead of copying the whole landscape into every query.
[[nodiscard]] std::optional<PreciseSweepHit>
sweep_terrain(const OdmTerrain& terrain, SweepVector from, SweepVector to, double radius) {
    constexpr int kDim = OdmTerrain::kGridDim;
    constexpr render::TerrainScale kScale{};
    constexpr double kCell = kScale.cell_size;
    constexpr double kHalf = (kDim - 1) * kCell * 0.5;
    const double lo_x = std::min(from.x, to.x) - radius;
    const double hi_x = std::max(from.x, to.x) + radius;
    const double lo_z = std::min(from.z, to.z) - radius;
    const double hi_z = std::max(from.z, to.z) + radius;
    if (hi_x < -kHalf || lo_x > kHalf || hi_z < -kHalf || lo_z > kHalf)
        return std::nullopt;
    const auto cell_at = [](double coordinate, bool lower = false) {
        const double grid = (coordinate + kHalf) / kCell;
        // Include both neighbors when the lower bound lies exactly on a seam.
        const double index = lower ? std::ceil(grid) - 1 : std::floor(grid);
        return static_cast<int>(std::clamp(index, 0.0, static_cast<double>(kDim - 2)));
    };
    const auto vertex = [&](int x, int z) {
        const auto index = static_cast<std::size_t>(z) * kDim + static_cast<std::size_t>(x);
        return Vec3{
            static_cast<float>(x * kCell - kHalf),
            static_cast<float>(terrain.heightmap[index]) * kScale.height_scale,
            static_cast<float>(z * kCell - kHalf),
        };
    };
    // A buried starting center must emerge above the landscape, not collide
    // with its underside. Recovery is vertical; subsequent sweeps use slopes.
    if (from.x >= -kHalf && from.x <= kHalf && from.z >= -kHalf && from.z <= kHalf) {
        const int x = cell_at(from.x);
        const int z = cell_at(from.z);
        const double fx = (from.x + kHalf) / kCell - x;
        const double fz = (from.z + kHalf) / kCell - z;
        const double a = vertex(x, z).y;
        const double b = vertex(x + 1, z).y;
        const double c = vertex(x + 1, z + 1).y;
        const double d = vertex(x, z + 1).y;
        const double height = fx + fz <= 1 ? a + (b - a) * fx + (d - a) * fz
                                           : c + (d - c) * (1 - fx) + (b - c) * (1 - fz);
        if (from.y <= height)
            return PreciseSweepHit{
                0,
                {0, 1, 0},
                std::min(height + radius - from.y,
                         static_cast<double>(std::numeric_limits<float>::max())),
            };
    }
    CollisionPolygon polygon;
    polygon.vertices.resize(3);
    std::optional<PreciseSweepHit> nearest;
    const auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        polygon.vertices[0] = a;
        polygon.vertices[1] = b;
        polygon.vertices[2] = c;
        polygon.normal = render::normalize(render::cross(c - a, b - a));
        if (const auto hit = sweep_polygon(polygon, from, to - from, radius);
            hit && (!nearest || hit->fraction < nearest->fraction))
            nearest = hit;
    };
    const int last_x = cell_at(hi_x);
    const int last_z = cell_at(hi_z);
    for (int z = cell_at(lo_z, true); z <= last_z; ++z) {
        for (int x = cell_at(lo_x, true); x <= last_x; ++x) {
            const auto a = vertex(x, z);
            const auto b = vertex(x + 1, z);
            const auto c = vertex(x + 1, z + 1);
            const auto d = vertex(x, z + 1);
            triangle(a, b, d);
            triangle(b, c, d);
        }
    }
    return nearest;
}

// Index of the largest component of the normal: the axis to drop when
// projecting the polygon to 2D.
[[nodiscard]] int dominant_axis(Vec3 n) {
    const float ax = std::fabs(n.x);
    const float ay = std::fabs(n.y);
    const float az = std::fabs(n.z);
    if (ax >= ay && ax >= az)
        return 0;
    return (ay >= az) ? 1 : 2;
}

// Drop the dominant axis, leaving a 2D point the winding test can use.
void project(Vec3 p, int axis, float& u, float& v) {
    switch (axis) {
    case 0:
        u = p.y;
        v = p.z;
        break;
    case 1:
        u = p.x;
        v = p.z;
        break;
    default:
        u = p.x;
        v = p.y;
        break;
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
    if (vertices.size() < 3 || !finite(normal) ||
        !std::ranges::all_of(vertices, [](Vec3 v) { return finite(v); })) {
        return;
    }
    const double len = std::sqrt(dot(precise(normal), precise(normal)));
    if (!(len > 1e-6)) {
        return;  // a degenerate normal cannot define a plane
    }

    CollisionPolygon poly;
    poly.vertices.assign(vertices.begin(), vertices.end());
    poly.normal = rounded(precise(normal) * (1.0 / len));
    // Anchor the plane on the first vertex.
    const Vec3 a = poly.vertices.front();
    poly.distance = -(poly.normal.x * a.x + poly.normal.y * a.y + poly.normal.z * a.z);
    if (!std::isfinite(poly.distance))
        return;

    poly.lo = poly.hi = a;
    for (const Vec3& v : poly.vertices) {
        poly.lo = {std::min(poly.lo.x, v.x), std::min(poly.lo.y, v.y), std::min(poly.lo.z, v.z)};
        poly.hi = {std::max(poly.hi.x, v.x), std::max(poly.hi.y, v.y), std::max(poly.hi.z, v.z)};
    }
    polygons_.push_back(std::move(poly));
}

std::optional<SphereSweepHit> CollisionWorld::sweep_sphere(Vec3 from, Vec3 to, float radius,
                                                           const OdmTerrain* terrain) const {
    if (!finite(from) || !finite(to) || !std::isfinite(radius) || radius < 0.0f)
        return std::nullopt;
    const auto start = precise(from);
    const auto finish = precise(to);
    const auto movement = finish - start;
    const double r = radius;
    std::optional<PreciseSweepHit> nearest;
    for (const auto& polygon : polygons_) {
        if (std::max(start.x, finish.x) + r < polygon.lo.x ||
            std::min(start.x, finish.x) - r > polygon.hi.x ||
            std::max(start.y, finish.y) + r < polygon.lo.y ||
            std::min(start.y, finish.y) - r > polygon.hi.y ||
            std::max(start.z, finish.z) + r < polygon.lo.z ||
            std::min(start.z, finish.z) - r > polygon.hi.z)
            continue;
        if (const auto hit = sweep_polygon(polygon, start, movement, r);
            hit && (!nearest || hit->fraction < nearest->fraction))
            nearest = hit;
    }
    bool terrain_hit = false;
    if (terrain != nullptr) {
        if (const auto hit = sweep_terrain(*terrain, start, finish, r);
            hit && (!nearest || hit->fraction < nearest->fraction)) {
            nearest = hit;
            terrain_hit = true;
        }
    }
    if (!nearest)
        return std::nullopt;
    return SphereSweepHit{
        static_cast<float>(nearest->fraction),
        rounded(start + movement * nearest->fraction),
        rounded(nearest->normal),
        static_cast<float>(nearest->penetration),
        terrain_hit,
    };
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
        const float y = -(poly.normal.x * from.x + poly.normal.z * from.z + poly.distance) / denom;
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

        // The bounds of the whole step, computed once rather than per polygon.
        // They have to cover where the move started as well as where it ends:
        // a fast step is stopped by comparing the two, and dismissing a wall
        // the destination has already passed through would let it tunnel.
        const float lo_x = std::min(from.x, pos.x) - radius;
        const float hi_x = std::max(from.x, pos.x) + radius;
        const float lo_z = std::min(from.z, pos.z) - radius;
        const float hi_z = std::max(from.z, pos.z) + radius;
        const float lo_y = std::min(from.y, pos.y);
        const float hi_y = std::max(from.y, pos.y) + height;

        for (const auto& poly : polygons_) {
            if (poly.normal.y >= kFloorNormalY) {
                continue;  // floors are handled by gravity, not by pushing
            }
            // Nowhere near the whole step.
            if (hi_x < poly.lo.x || lo_x > poly.hi.x || hi_z < poly.lo.z || lo_z > poly.hi.z ||
                hi_y < poly.lo.y || lo_y > poly.hi.y) {
                continue;
            }
            for (const float dy : samples) {
                const Vec3 probe{pos.x, pos.y + dy, pos.z};
                const float d = poly.normal.x * probe.x + poly.normal.y * probe.y +
                                poly.normal.z * probe.z + poly.distance;
                if (d >= radius) {
                    continue;  // still clear of this wall
                }
                // Faces are one-sided: only block someone who was in front of
                // the wall to begin with. Testing the start position is also
                // what stops a fast step from tunnelling clean through, since
                // `d` alone cannot tell "just short of" from "already past".
                const Vec3 start{from.x, from.y + dy, from.z};
                const float d_from = poly.normal.x * start.x + poly.normal.y * start.y +
                                     poly.normal.z * start.z + poly.distance;
                if (d_from < 0.0f) {
                    continue;
                }
                // The nearest point on the plane must be within the polygon,
                // otherwise the wall does not extend to where the player is.
                const Vec3 nearest{probe.x - poly.normal.x * d, probe.y - poly.normal.y * d,
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
