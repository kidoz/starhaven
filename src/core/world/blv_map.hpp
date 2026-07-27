#ifndef STARHAVEN_CORE_WORLD_BLV_MAP_HPP
#define STARHAVEN_CORE_WORLD_BLV_MAP_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/world/face_flags.hpp"

namespace starhaven::world {

// Outcome of parsing a `.blv` indoor map. The parser never throws.
enum class BlvError {
    None,
    // Input too small for the 8-byte zlib wrapper.
    TooSmall,
    // zlib inflate failed.
    InflateFailed,
    // The inflated length does not match the wrapper's declared size.
    SizeMismatch,
    // The payload is shorter than the fixed header.
    HeaderTooSmall,
    // A section (vertices, faces, index arrays, names) runs past the payload.
    Truncated,
    // A face's vertex-index list references a vertex the map does not have, or
    // the index arrays do not sum to the size the header declares.
    BadGeometry,
};

// The fixed header at the start of the decompressed payload.
// See docs/formats/blv.md.
struct BlvHeader {
    std::uint32_t kind = 0;  // 1 on most levels, 6 on some; meaning unknown
    std::string name;        // e.g. "Dwarf Hold", "No Name Level"
    std::string name2;       // e.g. "war1a", "test"

    // Total bytes of the per-face index-array block. This is a self-check: it
    // must equal sum(vertex_count + 1) * 2 * 6 over all faces.
    std::uint32_t index_block_bytes = 0;

    // Two further counts whose meaning is not yet established.
    std::uint32_t unknown_6c = 0;
    std::uint32_t unknown_70 = 0;

    // How many bytes of saved state the paired `.dlv` event file carries after
    // its fixed 200-slot block and before its 256-byte trailer. The level
    // declares it here, in the other file — which is what makes the event
    // file's size structural rather than incidental. Holds on all 52 maps;
    // see docs/formats/blv.md.
    std::uint32_t event_state_bytes = 0;
};

// One vertex of the level geometry. Indoor coordinates are 16-bit, unlike the
// 32-bit coordinates outdoor model meshes use.
struct BlvVertex {
    std::int16_t x = 0, y = 0, z = 0;
};

// The attribute bit that says a face has an extra record. It is an exact
// discriminator: across the 52 shipped maps 35,433 faces set it and every one
// has an extra, while no face without it has one.
inline constexpr std::uint32_t kBlvFaceHasExtra = 0x80000000u;

// One face (polygon) of the level.
struct BlvFace {
    // The face plane in 16.16 fixed point. The normal is unit length and
    // `normal · v + plane_distance == 0` holds for every vertex of the face.
    std::int32_t normal_x = 0, normal_y = 0, normal_z = 0;
    std::int32_t plane_distance = 0;

    std::uint32_t attributes = 0;
    std::uint8_t vertex_count = 0;

    // Indices into the map's vertex array, `vertex_count` of them.
    std::vector<std::uint16_t> vertex_ids;

    // Per-vertex texture coordinates in texels, parallel to `vertex_ids`.
    std::vector<std::int16_t> u;
    std::vector<std::int16_t> v;

    // A BITMAPS.LOD entry name, or empty for a face carrying no texture.
    std::string texture_name;

    // Attribute bit 0 marks a face the renderer should not draw. Every
    // untextured face in the shipped maps sets it (see docs/formats/blv.md).
    [[nodiscard]] bool invisible() const noexcept { return (attributes & 1u) != 0; }

    // Whether a face-extra record describes this face.
    [[nodiscard]] bool has_extra() const noexcept { return (attributes & kBlvFaceHasExtra) != 0; }

    // The plane this face is projected onto for two-dimensional work.
    [[nodiscard]] ProjectionPlane projection() const noexcept {
        return projection_plane(attributes);
    }

    [[nodiscard]] float nx() const noexcept { return static_cast<float>(normal_x) / 65536.0f; }
    [[nodiscard]] float ny() const noexcept { return static_cast<float>(normal_y) / 65536.0f; }
    [[nodiscard]] float nz() const noexcept { return static_cast<float>(normal_z) / 65536.0f; }
};

// One entry of the array that follows the face texture names. Each references
// a face; the shipped maps have far fewer of these than faces, so they are
// per-face *extra* data rather than a parallel array.
struct BlvFaceExtra {
    std::uint16_t face_index = 0;  // always a valid index into `faces`

    // The face's texture origin: the offset that brings its lowest texture
    // coordinate to zero. Where the field is set it equals -min(u) in 97.1% of
    // records and -min(v) in 99.0% (see docs/formats/blv.md). Zero means the
    // record carries no origin, which is not the same as an origin of zero.
    std::int16_t texture_origin_u = 0;
    std::int16_t texture_origin_v = 0;

    // Sparse; non-zero on about one record in six. Meaning unknown.
    std::uint16_t unknown_1a = 0;

    // A 10-byte name, parallel to the extras exactly as face texture names are
    // parallel to faces. Almost always empty: 12,184 of the 12,198 slots across
    // the shipped maps carry nothing.
    std::string name;
};

// A parsed `.blv` indoor map.
struct BlvMap {
    BlvHeader header;
    std::vector<BlvVertex> vertices;
    std::vector<BlvFace> faces;
    std::vector<BlvFaceExtra> face_extras;
    std::vector<std::uint8_t> payload;  // the whole decompressed payload

    // The address the payload was loaded at in the process that wrote the
    // file, recovered from the faces' stale array pointers: face N's first
    // pointer is `pointer_base + index_block_offset + running_offset`. Zero
    // when the faces disagree, which no shipped map does. It maps any other
    // stale pointer in the file to a payload offset. See docs/formats/blv.md.
    std::uint32_t pointer_base = 0;

    // Where the face-extra array starts in `payload`. Most of each 36-byte
    // record is still unidentified, so the raw bytes stay reachable.
    std::uint64_t face_extras_offset = 0;

    // How far into the payload this slice decodes. Everything after is the
    // room/BSP/light/door data a later slice will cover.
    std::uint64_t decoded_bytes = 0;
};

constexpr std::uint32_t kBlvDecorationSize = 32;        // the name records
constexpr std::uint32_t kBlvDecorationRecordSize = 28;  // the placement records
constexpr std::size_t kBlvDecorationNameSize = 0x16;

// One placed decoration: a named sprite (torch, barrel, tree, campfire) or a
// marker such as the party's start point.
struct BlvDecoration {
    std::string name;
    std::uint16_t flags = 0;  // 0 or 1; meaning unknown
    std::int32_t x = 0, y = 0, z = 0;
    std::int16_t angle = 0;  // facing; units unconfirmed
};

// Where a map's decoration block is, and how many decorations it holds.
//
// The block has the same shape as an outdoor map's: a count, then that many
// 28-byte records carrying a kind and 32-bit coordinates, then that many
// 32-byte records carrying the name. See docs/formats/blv.md.
struct BlvDecorationBlock {
    std::size_t offset = 0;  // of the count
    std::uint32_t count = 0;

    [[nodiscard]] bool found() const noexcept { return count > 0; }
    [[nodiscard]] std::size_t records() const noexcept { return offset + 4; }
    [[nodiscard]] std::size_t names() const noexcept {
        return records() + static_cast<std::size_t>(count) * kBlvDecorationRecordSize;
    }
    [[nodiscard]] std::size_t end() const noexcept {
        return names() + static_cast<std::size_t>(count) * kBlvDecorationSize;
    }
};

// Find that block. Returns a block with `found() == false` when no offset in
// the undecoded region begins one.
[[nodiscard]] BlvDecorationBlock find_decoration_block(const BlvMap& map);

// Locate a map's decoration array.
//
// This is a **scan, not a decode**: the sections between the face texture
// names and the decorations (rooms, BSP, lights, doors) are still unknown, so
// there is no offset or count to compute the array's position from. The scan
// looks for a run of records whose names are printable and whose coordinates
// fall inside the level's own vertex extents, which in practice is a strong
// filter — but it can find nothing on a map whose decorations happen not to
// match, and callers must treat an empty result as "not found" rather than
// "none present".
//
// Returns the records in file order.
[[nodiscard]] std::vector<BlvDecoration> find_decorations(const BlvMap& map);

// Where that scan believes the decoration array begins, or the payload size
// when it finds none. Exposed for research: the region between `decoded_bytes`
// and this is exactly what is still unknown.
[[nodiscard]] std::size_t find_decorations_offset(const BlvMap& map);

constexpr std::uint32_t kBlvFaceExtraSize = 36;
constexpr std::uint32_t kBlvFaceExtraNameSize = 10;

// Parse a raw `.blv` entry (as read from Games.lod). Handles the 8-byte zlib
// wrapper, the header, the vertex array, the face array, the per-face index
// arrays and the face texture names.
//
// Sections that follow the texture names (rooms, BSP, lights, doors) are left
// in `payload` for a later slice.
[[nodiscard]] BlvError parse_blv(std::span<const std::byte> entry, BlvMap& out);

// Layout constants (see docs/formats/blv.md).
constexpr std::uint32_t kBlvWrapperSize = 8;
constexpr std::uint32_t kBlvVertexCountOffset = 0x88;
constexpr std::uint32_t kBlvVertexSize = 6;      // 3 x i16
constexpr std::uint32_t kBlvFaceSize = 80;       // 0x50
constexpr std::uint32_t kBlvFaceArrayCount = 6;  // index arrays per face
constexpr std::uint32_t kBlvTextureNameSize = 10;
constexpr std::uint32_t kBlvHeaderSize = 0x8C;  // through the vertex count

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_BLV_MAP_HPP
