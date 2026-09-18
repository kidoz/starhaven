#ifndef STARHAVEN_CORE_WORLD_COLLISION_HPP
#define STARHAVEN_CORE_WORLD_COLLISION_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/render/math3d.hpp"

namespace starhaven::world {

struct OdmTerrain;

// A convex polygon of the collision world, in renderer axes (Y up).
struct CollisionPolygon {
    std::vector<render::Vec3> vertices;
    render::Vec3 normal{0, 1, 0};  // unit length
    float distance = 0.0f;         // plane: dot(normal, p) + distance == 0

    // The polygon's own bounds, kept so that a query can dismiss it without
    // touching its plane. A map with four hundred monsters asks this question
    // four hundred times a frame, and most polygons are nowhere near.
    render::Vec3 lo{};
    render::Vec3 hi{};
};

struct SphereSweepHit {
    float fraction = 0.0f;     // [0, 1] along the requested movement
    render::Vec3 position;     // sphere center, not the point on the surface
    render::Vec3 normal;       // unit normal from the surface toward the sphere
    float penetration = 0.0f;  // initial overlap depth; zero for later contact
    bool terrain = false;
};

// A static collision world: the level's polygons, queried for standing on and
// walking into. Built once per map from geometry the format decoders produce,
// so indoor faces and outdoor model facets share one implementation.
//
// Queries are brute force over all polygons. At MM6's scale — a few thousand
// per level — that is far cheaper per frame than the rasterization already
// being done, and it avoids depending on the spatial structures that are still
// undecoded.
class CollisionWorld {
public:
    // `normal` need not be normalized; degenerate polygons are ignored.
    void add_polygon(std::span<const render::Vec3> vertices, render::Vec3 normal);

    [[nodiscard]] std::size_t size() const noexcept { return polygons_.size(); }
    [[nodiscard]] const std::vector<CollisionPolygon>& polygons() const noexcept {
        return polygons_;
    }

    // Highest floor at or below `from`, directly under it. Returns false when
    // nothing is underfoot, which the caller should treat as "keep falling"
    // rather than "teleport".
    [[nodiscard]] bool floor_below(render::Vec3 from, float& out_y) const;

    // Move a vertical cylinder from `from` to `to`, pushing out of any wall it
    // would end up inside. The push is along the wall normal, so movement into
    // a wall at an angle slides along it rather than stopping dead.
    [[nodiscard]] render::Vec3 slide(render::Vec3 from, render::Vec3 to, float radius,
                                     float height) const;

    // Earliest contact with either side, edge or vertex of a polygon. This
    // is StarHaven geometry policy, independent of the walking-cylinder rules.
    // Initial overlap/touch returns fraction zero, including movement away;
    // a stationary sphere therefore reports only existing contact. At an
    // exact surface center, the normal opposes motion along the polygon normal,
    // or uses that normal when motion is parallel/zero. Equal-time ties retain
    // polygon insertion order. For initial overlap, normal * penetration moves
    // the center out of the chosen surface; other surfaces may still overlap.
    // Callers resolve overlap before sweeping again.
    // Zero radius is a point sweep. Negative radius or non-finite inputs return
    // no hit. Optional terrain uses the renderer's grid scale and triangle
    // split, querying only cells touched by the swept bounds. Buried centers
    // recover vertically to the surface plus radius. Terrain ends at the grid
    // boundary; water tiles currently act as solid ground. Equal-time ties
    // prefer polygons. This is engine policy, not original outdoor arithmetic.
    // No position or world state is changed by the query.
    [[nodiscard]] std::optional<SphereSweepHit>
    sweep_sphere(render::Vec3 from, render::Vec3 to, float radius,
                 const OdmTerrain* terrain = nullptr) const;

private:
    std::vector<CollisionPolygon> polygons_;
};

// True if `point` lies inside `polygon` when both are projected onto the plane
// that the polygon's normal is most perpendicular to. Exposed for tests.
[[nodiscard]] bool point_in_polygon(const CollisionPolygon& polygon, render::Vec3 point);

// A surface this close to horizontal counts as floor; anything steeper is wall.
inline constexpr float kFloorNormalY = 0.5f;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_COLLISION_HPP
