#ifndef OPENMM6_CORE_RENDER_MATH3D_HPP
#define OPENMM6_CORE_RENDER_MATH3D_HPP

#include <array>
#include <cmath>

namespace openmm6::render {

// Minimal 3D math for the software rasterizer. Plain float, fixed-size, no
// dependencies. Matrices are column-major (column c, row r at [c*4 + r]) so
// that `mat4 * vec4` is a sequence of column dots — the standard graphics
// convention. Vectors are row vectors in memory (x,y,z,w).

// A 2-component vector. Used for texture coordinates, where u runs left to
// right and v runs top to bottom (matching the decoded image row order).
struct Vec2 {
    float u = 0, v = 0;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

// 4x4 matrix, column-major storage.
struct Mat4 {
    std::array<float, 16> m{};  // [column*4 + row]
    float operator()(int col, int row) const { return m[col * 4 + row]; }
    float& operator()(int col, int row) { return m[col * 4 + row]; }
};

// --- Vec3 ops ---
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 0 ? Vec3{a.x / len, a.y / len, a.z / len} : a;
}

// --- Mat4 ops ---
inline Mat4 mat4_identity() {
    Mat4 r{};
    r(0, 0) = r(1, 1) = r(2, 2) = r(3, 3) = 1.0f;
    return r;
}

// Perspective projection. fov_y in radians. Maps view-space z in [-far,-near]
// to NDC z in [0,1] (0 at the near plane, 1 at the far plane), so the rasterizer
// z-buffer is a plain [0,1] comparison. x/y map to [-1,1]. clip.w = -z_view.
inline Mat4 mat4_perspective(float fov_y, float aspect, float near_p, float far_p) {
    Mat4 r{};
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    r(0, 0) = f / aspect;
    r(1, 1) = f;
    r(2, 2) = -far_p / (far_p - near_p);
    r(3, 2) = -far_p * near_p / (far_p - near_p);
    r(2, 3) = -1.0f;  // clip.w = -z_view (positive for visible z<0)
    return r;
}

// Translation.
inline Mat4 mat4_translate(Vec3 t) {
    Mat4 r = mat4_identity();
    r(3, 0) = t.x; r(3, 1) = t.y; r(3, 2) = t.z;
    return r;
}

// Rotation about Y (yaw), then X (pitch), then Z (roll) — applied as separate
// matrices multiplied. Angles in radians.
inline Mat4 mat4_rotation_y(float rad) {
    Mat4 r = mat4_identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r(0, 0) = c;  r(2, 0) = s;
    r(0, 2) = -s; r(2, 2) = c;
    return r;
}
inline Mat4 mat4_rotation_x(float rad) {
    Mat4 r = mat4_identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r(1, 1) = c;  r(2, 1) = -s;
    r(1, 2) = s;  r(2, 2) = c;
    return r;
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += a(k, row) * b(c, k);
            r(c, row) = sum;
        }
    return r;
}

// Transform a Vec4 by a Mat4 (M * v).
inline Vec4 operator*(const Mat4& m, Vec4 v) {
    return {
        m(0, 0) * v.x + m(1, 0) * v.y + m(2, 0) * v.z + m(3, 0) * v.w,
        m(0, 1) * v.x + m(1, 1) * v.y + m(2, 1) * v.z + m(3, 1) * v.w,
        m(0, 2) * v.x + m(1, 2) * v.y + m(2, 2) * v.z + m(3, 2) * v.w,
        m(0, 3) * v.x + m(1, 3) * v.y + m(2, 3) * v.z + m(3, 3) * v.w,
    };
}

// Build a view matrix for a camera at `eye` looking toward `target` with `up`.
// Right-handed look-at (camera looks down -Z in view space).
inline Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);   // forward
    const Vec3 s = normalize(cross(f, up));   // right
    const Vec3 u = cross(s, f);               // true up
    Mat4 r = mat4_identity();
    r(0, 0) = s.x; r(0, 1) = u.x; r(0, 2) = -f.x;
    r(1, 0) = s.y; r(1, 1) = u.y; r(1, 2) = -f.y;
    r(2, 0) = s.z; r(2, 1) = u.z; r(2, 2) = -f.z;
    r(3, 0) = -dot(s, eye);
    r(3, 1) = -dot(u, eye);
    r(3, 2) = dot(f, eye);
    return r;
}

// Forward/right/up vectors from yaw (about Y) and pitch (about X), radians.
// Forward points where the camera looks.
inline Vec3 camera_forward(float yaw, float pitch) {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw),   sy = std::sin(yaw);
    // yaw=0 looks down -Z; pitch>0 looks up.
    return {sy * cp, sp, -cp * cy};
}
inline Vec3 camera_right(float yaw) {
    return {std::cos(yaw), 0, std::sin(yaw)};  // yaw=0 -> +X
}

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float deg) { return deg * kPi / 180.0f; }

}  // namespace openmm6::render

#endif  // OPENMM6_CORE_RENDER_MATH3D_HPP
