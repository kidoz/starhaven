#ifndef STARHAVEN_CORE_WORLD_FACE_FLAGS_HPP
#define STARHAVEN_CORE_WORLD_FACE_FLAGS_HPP

#include <cstdint>

namespace starhaven::world {

// The plane a polygon is projected onto to work in two dimensions — for
// point-in-polygon tests and for texture mapping. Every indoor face and every
// outdoor model facet declares one.
enum class ProjectionPlane : std::uint8_t {
    XY,  // the face is most nearly horizontal; z is dropped
    XZ,  // most nearly aligned with the y axis
    YZ,  // most nearly aligned with the x axis
};

// The attribute bits that carry it. Exactly one is set on every polygon.
inline constexpr std::uint32_t kFaceProjectXY = 0x00000100u;
inline constexpr std::uint32_t kFaceProjectXZ = 0x00000200u;
inline constexpr std::uint32_t kFaceProjectYZ = 0x00000400u;
inline constexpr std::uint32_t kFaceProjectMask = kFaceProjectXY | kFaceProjectXZ | kFaceProjectYZ;

// Which plane a polygon's attributes select. Defaults to YZ when none of the
// three bits is set, which no shipped polygon does.
[[nodiscard]] inline ProjectionPlane projection_plane(std::uint32_t attributes) noexcept {
    if ((attributes & kFaceProjectXY) != 0) {
        return ProjectionPlane::XY;
    }
    if ((attributes & kFaceProjectXZ) != 0) {
        return ProjectionPlane::XZ;
    }
    return ProjectionPlane::YZ;
}

// The plane a polygon with this normal should declare: the one perpendicular to
// its largest component, with ties broken towards z, then y.
//
// This reproduces the shipped data exactly — 89,091 indoor faces and 37,187
// outdoor facets, with no exceptions — which is what identifies the three bits.
// See docs/formats/blv.md.
[[nodiscard]] inline ProjectionPlane projection_plane_for(float nx, float ny, float nz) noexcept {
    const float ax = nx < 0 ? -nx : nx;
    const float ay = ny < 0 ? -ny : ny;
    const float az = nz < 0 ? -nz : nz;
    if (az >= ay && az >= ax) {
        return ProjectionPlane::XY;
    }
    if (ay >= ax) {
        return ProjectionPlane::XZ;
    }
    return ProjectionPlane::YZ;
}

// Attribute bits that accompany a non-zero event id in a face-extra record.
// The association is close but not exact — 5,380 of 5,560 — so this is a mask
// to test against, not a rule to rely on. See docs/formats/blv.md.
inline constexpr std::uint32_t kFaceEventMask = 0x02000000u | 0x04000000u | 0x08000000u;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_FACE_FLAGS_HPP
