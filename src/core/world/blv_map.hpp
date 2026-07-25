#ifndef STARHAVEN_CORE_WORLD_BLV_MAP_HPP
#define STARHAVEN_CORE_WORLD_BLV_MAP_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

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
    std::uint32_t kind = 0;      // 1 on most levels, 6 on some; meaning unknown
    std::string name;            // e.g. "Dwarf Hold", "No Name Level"
    std::string name2;           // e.g. "war1a", "test"

    // Total bytes of the per-face index-array block. This is a self-check: it
    // must equal sum(vertex_count + 1) * 2 * 6 over all faces.
    std::uint32_t index_block_bytes = 0;

    // Three further counts whose meaning is not yet established.
    std::uint32_t unknown_6c = 0;
    std::uint32_t unknown_70 = 0;
    std::uint32_t unknown_74 = 0;
};

// One vertex of the level geometry. Indoor coordinates are 16-bit, unlike the
// 32-bit coordinates outdoor model meshes use.
struct BlvVertex {
    std::int16_t x = 0, y = 0, z = 0;
};

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

    // A BITMAPS.LOD entry name, or empty for a face carrying no texture.
    std::string texture_name;

    // Attribute bit 0 marks a face the renderer should not draw. Every
    // untextured face in the shipped maps sets it (see docs/formats/blv.md).
    [[nodiscard]] bool invisible() const noexcept { return (attributes & 1u) != 0; }

    [[nodiscard]] float nx() const noexcept { return static_cast<float>(normal_x) / 65536.0f; }
    [[nodiscard]] float ny() const noexcept { return static_cast<float>(normal_y) / 65536.0f; }
    [[nodiscard]] float nz() const noexcept { return static_cast<float>(normal_z) / 65536.0f; }
};

// A parsed `.blv` indoor map.
struct BlvMap {
    BlvHeader header;
    std::vector<BlvVertex> vertices;
    std::vector<BlvFace> faces;
    std::vector<std::uint8_t> payload;  // the whole decompressed payload

    // How far into the payload this slice decodes. Everything after is the
    // room/BSP/light/door data a later slice will cover.
    std::uint64_t decoded_bytes = 0;
};

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
constexpr std::uint32_t kBlvVertexSize = 6;         // 3 x i16
constexpr std::uint32_t kBlvFaceSize = 80;          // 0x50
constexpr std::uint32_t kBlvFaceArrayCount = 6;     // index arrays per face
constexpr std::uint32_t kBlvTextureNameSize = 10;
constexpr std::uint32_t kBlvHeaderSize = 0x8C;      // through the vertex count

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_BLV_MAP_HPP
