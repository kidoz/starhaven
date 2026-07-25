#ifndef STARHAVEN_CORE_WORLD_ODM_MAP_HPP
#define STARHAVEN_CORE_WORLD_ODM_MAP_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// One ground tileset reference: a group id and an offset into the tileset
// table (see docs/formats/odm.md). Four of these follow the header strings.
struct OdmTilesetDef {
    std::int16_t group = 0;   // tileset group id
    std::int16_t offset = 0;  // tileset offset
};

// The fixed map header parsed from the decompressed ODM payload.
// Layout verified against the engine struct (see docs/formats/odm.md):
//   name[32], file_name[32], version[31]+pad, reserved[32], ground_name[32],
//   tilesets[4] (each: i16 group, i16 offset).
struct OdmHeader {
    std::string name;          // offset 0x00, e.g. "blank"
    std::string file_name;     // offset 0x20, e.g. "default.odm"
    std::string version;       // offset 0x40, e.g. "MM6 Outdoor v1.11"
    std::string ground_name;   // offset 0x80, e.g. "grastyl" (ground tileset)
    std::array<OdmTilesetDef, 4> tilesets{};  // offset 0xA0 (4 tileset defs)

    // Backwards-compatible access to the tileset u16 fields as a flat array
    // (group0, offset0, group1, offset1, ...). Prefer `tilesets` in new code.
    [[nodiscard]] std::array<std::uint16_t, 8> dim_fields() const noexcept {
        std::array<std::uint16_t, 8> out{};
        for (std::size_t i = 0; i < 4; ++i) {
            out[i * 2]     = static_cast<std::uint16_t>(tilesets[i].group);
            out[i * 2 + 1] = static_cast<std::uint16_t>(tilesets[i].offset);
        }
        return out;
    }
};

// Outcome of parsing. Callers convert these into user-facing text; the parser
// never throws.
enum class OdmError {
    None,
    // Input too small for the 8-byte zlib wrapper.
    TooSmall,
    // zlib inflate failed.
    InflateFailed,
    // The inflated length does not match the wrapper's decompressedSize.
    SizeMismatch,
    // The decompressed payload is shorter than the fixed header.
    HeaderTooSmall,
    // The version string is not a recognized "MM6 Outdoor" form.
    UnsupportedVersion,
};

// A parsed ODM map: its header plus the full decompressed payload (the geometry
// and tile data beyond the header are an opaque blob in this slice).
struct OdmMap {
    OdmHeader header;
    std::vector<std::uint8_t> payload;  // the whole decompressed payload
};

// Parse a raw .odm entry (as read from Games.lod) into an OdmMap. Handles the
// 8-byte zlib wrapper and the fixed header. The payload blob is exposed for
// later slices to decode geometry and tiles.
[[nodiscard]] OdmError parse_odm(std::span<const std::byte> entry, OdmMap& out);

// --- Terrain grids (see docs/formats/odm-terrain.md) -----------------------

// The two 128x128 byte grids that define outdoor terrain elevation and ground
// tile selection. Both are row-major: index (x,y) as grid[y*128 + x].
struct OdmTerrain {
    static constexpr std::uint16_t kGridDim = 128;
    static constexpr std::size_t kGridBytes = static_cast<std::size_t>(kGridDim) * kGridDim;

    std::array<std::uint8_t, kGridBytes> heightmap{};  // offset 0xB0
    std::array<std::uint8_t, kGridBytes> tilemap{};    // offset 0x40B0
};

// Extract the heightmap and tilemap from an already-parsed OdmMap's payload.
// Returns HeaderTooSmall if the payload cannot hold both grids.
[[nodiscard]] OdmError extract_terrain(const OdmMap& map, OdmTerrain& out);

// Convenience: parse a raw .odm entry and extract its terrain in one call.
[[nodiscard]] OdmError parse_odm_terrain(std::span<const std::byte> entry,
                                         OdmMap& map_out, OdmTerrain& terrain_out);

// --- Placed models (see docs/formats/odm-models.md) -----------------------

// One placed map model: a named prop/building with a world position and an
// axis-aligned bounding box. The model's own mesh (nested vertices/facets/BSP,
// referenced by offsets inside the on-disk record) is decoded in a later slice.
struct OdmModel {
    std::string name;
    std::string name2;
    std::int32_t pos_x = 0, pos_y = 0, pos_z = 0;       // world position
    std::int32_t min_x = 0, min_y = 0, min_z = 0;       // bounding box min
    std::int32_t max_x = 0, max_y = 0, max_z = 0;       // bounding box max
};

// Extract the model array (placed props/buildings) from an OdmMap's payload.
// The model count lives at offset 0xC0B0; each model is a 188-byte record.
[[nodiscard]] OdmError extract_models(const OdmMap& map,
                                      std::vector<OdmModel>& out);

// Constants for the model section (see docs/formats/odm-models.md).
constexpr std::uint32_t kModelCountOffset = 0xC0B0;  // = 0xB0 + 3*0x4000
constexpr std::uint32_t kModelRecordSize = 188;       // 0xBC

// --- Model mesh vertices (see docs/formats/odm-model-mesh.md) -------------

// One model-mesh vertex: world-space coordinates (the engine's ModelVertex,
// verified as 12-byte i32 x/y/z).
struct OdmModelVertex {
    std::int32_t x = 0, y = 0, z = 0;
};

// Read the vertex_count field (+0x44) of a model record by index. Returns
// false if the index is out of range.
[[nodiscard]] bool model_vertex_count(const OdmMap& map, std::size_t model_index,
                                      std::uint32_t& out);

// Extract the FIRST model's vertex array. The geometry section begins at
// 0xC0B4 + model_count*188, and the first model's vertices (vertex_count × 12
// bytes) are stored there. Walking subsequent models' vertices requires
// decoding the variable-length facet stream in between (a later slice).
[[nodiscard]] OdmError extract_first_model_vertices(
    const OdmMap& map, std::vector<OdmModelVertex>& out);

constexpr std::uint32_t kModelVertexSize = 12;  // 3 × i32

// --- Model meshes (see docs/formats/odm-model-facets.md) -------------------

// The fixed capacity of a facet's per-vertex arrays. A facet is a convex
// polygon of at most this many vertices; the on-disk record reserves the full
// capacity whatever the actual polygon size.
constexpr std::size_t kFacetMaxVertices = 20;

// One facet (polygon) of a model's mesh: an n-gon over that model's own vertex
// array, with a plane, per-vertex texture coordinates, and a texture name.
struct OdmModelFacet {
    // The facet plane, stored as 16.16 fixed point. The normal is unit length,
    // and `normal · v + plane_distance == 0` holds for every vertex v of the
    // facet — which is how this decode was verified.
    std::int32_t normal_x = 0, normal_y = 0, normal_z = 0;
    std::int32_t plane_distance = 0;

    std::uint32_t attributes = 0;   // bit flags; see the spec (partly unknown)
    std::uint8_t vertex_count = 0;  // the polygon's size; indexes the arrays below

    // Indices into the owning mesh's `vertices`. Only the first
    // `vertex_count` entries are meaningful.
    std::array<std::uint16_t, kFacetMaxVertices> vertex_ids{};

    // Per-vertex texture coordinates in texels, parallel to `vertex_ids`.
    std::array<std::int16_t, kFacetMaxVertices> u{};
    std::array<std::int16_t, kFacetMaxVertices> v{};

    // The facet's texture: a BITMAPS.LOD entry name. May be empty.
    std::string texture_name;

    // The plane with the 16.16 fixed point divided out, for renderers that
    // work in floats.
    [[nodiscard]] float nx() const noexcept { return static_cast<float>(normal_x) / 65536.0f; }
    [[nodiscard]] float ny() const noexcept { return static_cast<float>(normal_y) / 65536.0f; }
    [[nodiscard]] float nz() const noexcept { return static_cast<float>(normal_z) / 65536.0f; }
};

// One model's complete mesh: its own vertex array plus the facets over it.
// Facet vertex ids are local to this mesh, not global to the map.
struct OdmModelMesh {
    std::vector<OdmModelVertex> vertices;
    std::vector<OdmModelFacet> facets;
};

// Extract every model's mesh, in model-array order (so index i corresponds to
// `extract_models`' entry i).
//
// The geometry section is a single stream with no offset table: each model
// contributes vertices, facets, a facet-ordering array, BSP nodes, and facet
// texture names back to back, and the next model starts immediately after.
// Walking it is therefore all-or-nothing — a truncated or inconsistent stream
// is rejected rather than partially decoded.
[[nodiscard]] OdmError extract_model_meshes(const OdmMap& map,
                                            std::vector<OdmModelMesh>& out);

// --- Decorations (see docs/formats/odm-decorations.md) ---------------------

// One placed decoration: a sprite standing in the world (tree, barrel, sign).
struct OdmDecoration {
    std::uint32_t kind = 0;   // type id; maps one-to-one onto `name`
    std::int32_t x = 0, y = 0, z = 0;   // world position, MM6 axes
    std::string name;         // a SPRITES.LOD entry name
};

// Extract the decoration array, which follows the model geometry stream.
//
// Unlike the indoor maps, this is fully deterministic: the geometry stream's
// end is computable, and the count sits right there.
[[nodiscard]] OdmError extract_decorations(const OdmMap& map,
                                           std::vector<OdmDecoration>& out);

// Byte offset one past the model geometry stream: where the decorations begin.
[[nodiscard]] OdmError model_geometry_end(const OdmMap& map, std::uint64_t& out);

constexpr std::uint32_t kDecorationRecordSize = 28;  // kind + 3 x i32 + padding
constexpr std::uint32_t kDecorationNameSize = 32;

// Sizes of the per-model geometry arrays (see docs/formats/odm-model-facets.md).
constexpr std::uint32_t kModelFacetSize = 308;         // 0x134
constexpr std::uint32_t kFacetOrderingEntrySize = 2;   // u16 per facet
constexpr std::uint32_t kModelBspNodeSize = 8;         // per BSP node
constexpr std::uint32_t kFacetTextureNameSize = 10;    // char[10] per facet

// Fixed sizes from the verified spec, exposed for tests and tools.
constexpr std::uint32_t kWrapperSize = 8;
constexpr std::uint32_t kHeaderSize = 0xB0;  // name..dim_fields inclusive
constexpr std::uint32_t kNameFieldSize = 32;
constexpr std::uint32_t kDimFieldCount = 8;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_ODM_MAP_HPP
