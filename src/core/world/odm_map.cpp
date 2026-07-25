#include "core/world/odm_map.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace starhaven::world {

using image::detail::inflate_all;

namespace {

// Verified field offsets within the decompressed payload (see docs/formats/odm.md).
constexpr std::uint32_t kNameOff = 0x00;        // name[32]
constexpr std::uint32_t kFileNameOff = 0x20;    // file_name[32]
constexpr std::uint32_t kVersionOff = 0x40;     // version[31] + 1 pad byte
constexpr std::uint32_t kGroundNameOff = 0x80;  // ground_name[32]
constexpr std::uint32_t kTilesetsOff = 0xA0;    // 4 x (i16 group, i16 offset)

// Recognized version prefix. Real MM6 maps use "MM6 Outdoor v1.11".
constexpr std::string_view kExpectedVersionPrefix = "MM6 Outdoor";

[[nodiscard]] bool version_supported(const std::string& version) {
    // Accept any version beginning with the known MM6 outdoor prefix.
    if (version.size() < kExpectedVersionPrefix.size()) {
        return false;
    }
    return std::string_view{version}.substr(0, kExpectedVersionPrefix.size()) ==
           kExpectedVersionPrefix;
}

}  // namespace

OdmError parse_odm(std::span<const std::byte> entry, OdmMap& out) {
    out = OdmMap{};

    if (entry.size() < kWrapperSize) {
        return OdmError::TooSmall;
    }

    io::ByteReader r{entry};
    r.seek(0x00);
    // u32 at 0x00 is the size of the zlib stream that follows (stored size - 8).
    // u32 at 0x04 is the decompressed size. The decompressed size is the field
    // we validate against the inflate result.
    [[maybe_unused]] const std::uint32_t stream_size = r.read_u32_le();
    r.seek(0x04);
    const std::uint32_t decompressed_size = r.read_u32_le();
    (void)stream_size;

    // Inflate the zlib stream that starts at offset 8.
    const std::span<const std::byte> zlib_block = entry.subspan(kWrapperSize);
    if (!inflate_all(zlib_block, out.payload)) {
        return OdmError::InflateFailed;
    }

    // The inflated length must match the declared decompressed size. (If the
    // declared field is zero, treat as unknown and accept the inflated length.)
    if (decompressed_size != 0 && out.payload.size() != decompressed_size) {
        return OdmError::SizeMismatch;
    }

    if (out.payload.size() < kHeaderSize) {
        return OdmError::HeaderTooSmall;
    }

    // Read the fixed header fields from the decompressed payload. The payload is
    // uint8_t; wrap it for the byte reader.
    io::ByteReader h{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(out.payload.data()), out.payload.size()}};

    h.seek(kNameOff);
    if (!h.read_fixed_string(kNameFieldSize, out.header.name)) {
        return OdmError::HeaderTooSmall;
    }
    h.seek(kFileNameOff);
    if (!h.read_fixed_string(kNameFieldSize, out.header.file_name)) {
        return OdmError::HeaderTooSmall;
    }
    h.seek(kVersionOff);
    if (!h.read_fixed_string(kNameFieldSize - 1, out.header.version)) {
        return OdmError::HeaderTooSmall;
    }
    if (!version_supported(out.header.version)) {
        return OdmError::UnsupportedVersion;
    }
    h.seek(kGroundNameOff);
    if (!h.read_fixed_string(kNameFieldSize, out.header.ground_name)) {
        return OdmError::HeaderTooSmall;
    }

    // Four tileset definitions: each i16 group + i16 offset.
    h.seek(kTilesetsOff);
    for (std::size_t i = 0; i < out.header.tilesets.size(); ++i) {
        out.header.tilesets[i].group = static_cast<std::int16_t>(h.read_u16_le());
        out.header.tilesets[i].offset = static_cast<std::int16_t>(h.read_u16_le());
    }

    return OdmError::None;
}

// --- Terrain grids ---------------------------------------------------------

namespace {

// Verified terrain offsets within the decompressed payload
// (see docs/formats/odm-terrain.md).
constexpr std::uint32_t kHeightmapOff = 0xB0;
constexpr std::uint32_t kTilemapOff = 0x40B0;  // kHeightmapOff + 16384

[[nodiscard]] OdmError copy_grid(const std::vector<std::uint8_t>& payload,
                                 std::uint32_t offset,
                                 std::array<std::uint8_t, OdmTerrain::kGridBytes>& out) {
    const std::uint64_t end =
        static_cast<std::uint64_t>(offset) + OdmTerrain::kGridBytes;
    if (payload.size() < end) {
        return OdmError::HeaderTooSmall;  // payload too short for this grid
    }
    std::memcpy(out.data(), payload.data() + offset, OdmTerrain::kGridBytes);
    return OdmError::None;
}

}  // namespace

OdmError extract_terrain(const OdmMap& map, OdmTerrain& out) {
    out = OdmTerrain{};
    if (OdmError e = copy_grid(map.payload, kHeightmapOff, out.heightmap);
        e != OdmError::None) {
        return e;
    }
    return copy_grid(map.payload, kTilemapOff, out.tilemap);
}

OdmError parse_odm_terrain(std::span<const std::byte> entry,
                           OdmMap& map_out, OdmTerrain& terrain_out) {
    if (OdmError e = parse_odm(entry, map_out); e != OdmError::None) {
        return e;
    }
    return extract_terrain(map_out, terrain_out);
}

// --- Placed models ---------------------------------------------------------

OdmError extract_models(const OdmMap& map, std::vector<OdmModel>& out) {
    out.clear();

    const auto& p = map.payload;
    if (p.size() < kModelCountOffset + 4) {
        return OdmError::HeaderTooSmall;
    }

    io::ByteReader r{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(p.data()), p.size()}};
    r.seek(kModelCountOffset);
    const std::uint32_t count = r.read_u32_le();

    // The whole model array must fit.
    const std::uint64_t array_end =
        static_cast<std::uint64_t>(kModelCountOffset) + 4 +
        static_cast<std::uint64_t>(count) * kModelRecordSize;
    if (array_end > p.size()) {
        return OdmError::HeaderTooSmall;
    }

    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t off =
            kModelCountOffset + 4 + static_cast<std::uint64_t>(i) * kModelRecordSize;
        OdmModel m;
        r.seek(static_cast<std::size_t>(off));
        if (!r.read_fixed_string(32, m.name)) {
            return OdmError::HeaderTooSmall;
        }
        r.seek(static_cast<std::size_t>(off + 0x20));
        if (!r.read_fixed_string(32, m.name2)) {
            return OdmError::HeaderTooSmall;
        }
        // Position and bounding box (all i32, verified offsets).
        r.seek(static_cast<std::size_t>(off + 0x70));
        m.pos_x = r.read_i32_le();
        m.pos_y = r.read_i32_le();
        m.pos_z = r.read_i32_le();
        m.min_x = r.read_i32_le();
        m.min_y = r.read_i32_le();
        m.min_z = r.read_i32_le();
        m.max_x = r.read_i32_le();
        m.max_y = r.read_i32_le();
        m.max_z = r.read_i32_le();
        out.push_back(std::move(m));
    }
    return OdmError::None;
}

// --- Model mesh vertices ---------------------------------------------------

bool model_vertex_count(const OdmMap& map, std::size_t model_index,
                        std::uint32_t& out) {
    out = 0;
    if (map.payload.size() < kModelCountOffset + 4) {
        return false;
    }
    const std::uint32_t count =
        (static_cast<std::uint32_t>(map.payload[kModelCountOffset])) |
        (static_cast<std::uint32_t>(map.payload[kModelCountOffset + 1]) << 8) |
        (static_cast<std::uint32_t>(map.payload[kModelCountOffset + 2]) << 16) |
        (static_cast<std::uint32_t>(map.payload[kModelCountOffset + 3]) << 24);
    if (model_index >= count) {
        return false;
    }
    const std::size_t rec = kModelCountOffset + 4 + model_index * kModelRecordSize + 0x44;
    if (rec + 4 > map.payload.size()) {
        return false;
    }
    out = (static_cast<std::uint32_t>(map.payload[rec])) |
          (static_cast<std::uint32_t>(map.payload[rec + 1]) << 8) |
          (static_cast<std::uint32_t>(map.payload[rec + 2]) << 16) |
          (static_cast<std::uint32_t>(map.payload[rec + 3]) << 24);
    return true;
}

OdmError extract_first_model_vertices(const OdmMap& map,
                                      std::vector<OdmModelVertex>& out) {
    out.clear();

    std::vector<OdmModel> models;
    if (OdmError e = extract_models(map, models); e != OdmError::None) {
        return e;
    }
    if (models.empty()) {
        return OdmError::None;  // no models -> no vertices
    }

    std::uint32_t vcount = 0;
    if (!model_vertex_count(map, 0, vcount)) {
        return OdmError::HeaderTooSmall;
    }

    // Geometry section starts right after the model array.
    const std::uint64_t geo_start =
        kModelCountOffset + 4 +
        static_cast<std::uint64_t>(models.size()) * kModelRecordSize;
    const std::uint64_t verts_end = geo_start +
        static_cast<std::uint64_t>(vcount) * kModelVertexSize;
    if (verts_end > map.payload.size()) {
        return OdmError::HeaderTooSmall;
    }

    io::ByteReader r{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(map.payload.data()), map.payload.size()}};
    out.reserve(vcount);
    for (std::uint32_t i = 0; i < vcount; ++i) {
        r.seek(static_cast<std::size_t>(geo_start + i * kModelVertexSize));
        OdmModelVertex v;
        v.x = r.read_i32_le();
        v.y = r.read_i32_le();
        v.z = r.read_i32_le();
        out.push_back(v);
    }
    return OdmError::None;
}

// --- Model meshes ----------------------------------------------------------

namespace {

// Field offsets within a 308-byte facet record (docs/formats/odm-model-facets.md).
constexpr std::uint32_t kFacetPlaneOff = 0x00;      // 4 x i32, 16.16 fixed point
constexpr std::uint32_t kFacetAttributesOff = 0x1C;  // u32 bit flags
constexpr std::uint32_t kFacetVertexIdsOff = 0x20;   // u16[20]
constexpr std::uint32_t kFacetUOff = 0x48;           // i16[20]
constexpr std::uint32_t kFacetVOff = 0x70;           // i16[20]
constexpr std::uint32_t kFacetVertexCountOff = 0x12E;  // u8

// Read a model record's three geometry counts. Returns false if the record
// does not fit in the payload.
[[nodiscard]] bool read_model_counts(io::ByteReader& r, std::uint64_t record_off,
                                     std::uint32_t& vertex_count,
                                     std::uint32_t& facet_count,
                                     std::uint32_t& bsp_node_count) {
    if (!r.seek(static_cast<std::size_t>(record_off + 0x44))) return false;
    vertex_count = r.read_u32_le();
    if (!r.seek(static_cast<std::size_t>(record_off + 0x4C))) return false;
    facet_count = r.read_u32_le();
    if (!r.seek(static_cast<std::size_t>(record_off + 0x5C))) return false;
    bsp_node_count = r.read_u32_le();
    return r.ok();
}

}  // namespace

OdmError extract_model_meshes(const OdmMap& map, std::vector<OdmModelMesh>& out) {
    out.clear();

    const auto& p = map.payload;
    if (p.size() < kModelCountOffset + 4) {
        return OdmError::HeaderTooSmall;
    }

    io::ByteReader r{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(p.data()), p.size()}};
    if (!r.seek(kModelCountOffset)) {
        return OdmError::HeaderTooSmall;
    }
    const std::uint32_t model_count = r.read_u32_le();

    const std::uint64_t array_end =
        static_cast<std::uint64_t>(kModelCountOffset) + 4 +
        static_cast<std::uint64_t>(model_count) * kModelRecordSize;
    if (array_end > p.size()) {
        return OdmError::HeaderTooSmall;
    }

    // The geometry stream begins right after the model array and is walked
    // strictly sequentially; `cursor` is the running position in it.
    std::uint64_t cursor = array_end;
    out.reserve(model_count);

    for (std::uint32_t i = 0; i < model_count; ++i) {
        const std::uint64_t record_off =
            kModelCountOffset + 4 + static_cast<std::uint64_t>(i) * kModelRecordSize;
        std::uint32_t vertex_count = 0;
        std::uint32_t facet_count = 0;
        std::uint32_t bsp_node_count = 0;
        if (!read_model_counts(r, record_off, vertex_count, facet_count,
                               bsp_node_count)) {
            return OdmError::HeaderTooSmall;
        }

        // Size the whole block before reading any of it, in 64-bit arithmetic
        // so that a hostile count cannot wrap.
        const std::uint64_t verts_bytes =
            static_cast<std::uint64_t>(vertex_count) * kModelVertexSize;
        const std::uint64_t facets_bytes =
            static_cast<std::uint64_t>(facet_count) * kModelFacetSize;
        const std::uint64_t ordering_bytes =
            static_cast<std::uint64_t>(facet_count) * kFacetOrderingEntrySize;
        const std::uint64_t bsp_bytes =
            static_cast<std::uint64_t>(bsp_node_count) * kModelBspNodeSize;
        const std::uint64_t names_bytes =
            static_cast<std::uint64_t>(facet_count) * kFacetTextureNameSize;

        const std::uint64_t verts_off = cursor;
        const std::uint64_t facets_off = verts_off + verts_bytes;
        const std::uint64_t ordering_off = facets_off + facets_bytes;
        const std::uint64_t bsp_off = ordering_off + ordering_bytes;
        const std::uint64_t names_off = bsp_off + bsp_bytes;
        const std::uint64_t block_end = names_off + names_bytes;
        if (block_end > p.size()) {
            return OdmError::HeaderTooSmall;
        }

        OdmModelMesh mesh;
        mesh.vertices.reserve(vertex_count);
        for (std::uint32_t vi = 0; vi < vertex_count; ++vi) {
            if (!r.seek(static_cast<std::size_t>(
                    verts_off + static_cast<std::uint64_t>(vi) * kModelVertexSize))) {
                return OdmError::HeaderTooSmall;
            }
            OdmModelVertex v;
            v.x = r.read_i32_le();
            v.y = r.read_i32_le();
            v.z = r.read_i32_le();
            mesh.vertices.push_back(v);
        }

        mesh.facets.reserve(facet_count);
        for (std::uint32_t fi = 0; fi < facet_count; ++fi) {
            const std::uint64_t base = facets_off +
                static_cast<std::uint64_t>(fi) * kModelFacetSize;
            OdmModelFacet f;

            if (!r.seek(static_cast<std::size_t>(base + kFacetPlaneOff))) {
                return OdmError::HeaderTooSmall;
            }
            f.normal_x = r.read_i32_le();
            f.normal_y = r.read_i32_le();
            f.normal_z = r.read_i32_le();
            f.plane_distance = r.read_i32_le();

            if (!r.seek(static_cast<std::size_t>(base + kFacetAttributesOff))) {
                return OdmError::HeaderTooSmall;
            }
            f.attributes = r.read_u32_le();

            if (!r.seek(static_cast<std::size_t>(base + kFacetVertexCountOff))) {
                return OdmError::HeaderTooSmall;
            }
            const std::uint8_t n = r.read_u8();
            // A facet that claims more vertices than the record can hold is
            // malformed; clamping would silently invent geometry.
            if (n > kFacetMaxVertices) {
                return OdmError::HeaderTooSmall;
            }
            f.vertex_count = n;

            if (!r.seek(static_cast<std::size_t>(base + kFacetVertexIdsOff))) {
                return OdmError::HeaderTooSmall;
            }
            for (std::size_t k = 0; k < kFacetMaxVertices; ++k) {
                f.vertex_ids[k] = r.read_u16_le();
            }
            if (!r.seek(static_cast<std::size_t>(base + kFacetUOff))) {
                return OdmError::HeaderTooSmall;
            }
            for (std::size_t k = 0; k < kFacetMaxVertices; ++k) {
                f.u[k] = static_cast<std::int16_t>(r.read_u16_le());
            }
            if (!r.seek(static_cast<std::size_t>(base + kFacetVOff))) {
                return OdmError::HeaderTooSmall;
            }
            for (std::size_t k = 0; k < kFacetMaxVertices; ++k) {
                f.v[k] = static_cast<std::int16_t>(r.read_u16_le());
            }

            // Every referenced vertex must exist in this model's own array.
            for (std::size_t k = 0; k < n; ++k) {
                if (f.vertex_ids[k] >= vertex_count) {
                    return OdmError::HeaderTooSmall;
                }
            }

            if (!r.seek(static_cast<std::size_t>(
                    names_off + static_cast<std::uint64_t>(fi) * kFacetTextureNameSize))) {
                return OdmError::HeaderTooSmall;
            }
            // The field is reused memory: many entries carry stale bytes after
            // the terminator, so read the fixed width and stop at the NUL.
            if (!r.read_fixed_string(kFacetTextureNameSize, f.texture_name)) {
                return OdmError::HeaderTooSmall;
            }

            mesh.facets.push_back(std::move(f));
        }

        if (!r.ok()) {
            return OdmError::HeaderTooSmall;
        }
        out.push_back(std::move(mesh));
        cursor = block_end;
    }
    return OdmError::None;
}

// --- Decorations -----------------------------------------------------------

OdmError model_geometry_end(const OdmMap& map, std::uint64_t& out) {
    out = 0;

    const auto& p = map.payload;
    if (p.size() < kModelCountOffset + 4) {
        return OdmError::HeaderTooSmall;
    }
    io::ByteReader r{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(p.data()), p.size()}};
    if (!r.seek(kModelCountOffset)) {
        return OdmError::HeaderTooSmall;
    }
    const std::uint32_t model_count = r.read_u32_le();

    std::uint64_t cursor =
        static_cast<std::uint64_t>(kModelCountOffset) + 4 +
        static_cast<std::uint64_t>(model_count) * kModelRecordSize;
    if (cursor > p.size()) {
        return OdmError::HeaderTooSmall;
    }

    // Each model contributes the same five arrays the mesh decoder walks.
    for (std::uint32_t i = 0; i < model_count; ++i) {
        const std::uint64_t rec =
            kModelCountOffset + 4 + static_cast<std::uint64_t>(i) * kModelRecordSize;
        std::uint32_t vertex_count = 0;
        std::uint32_t facet_count = 0;
        std::uint32_t bsp_node_count = 0;
        if (!read_model_counts(r, rec, vertex_count, facet_count, bsp_node_count)) {
            return OdmError::HeaderTooSmall;
        }
        cursor += static_cast<std::uint64_t>(vertex_count) * kModelVertexSize;
        cursor += static_cast<std::uint64_t>(facet_count) * kModelFacetSize;
        cursor += static_cast<std::uint64_t>(facet_count) * kFacetOrderingEntrySize;
        cursor += static_cast<std::uint64_t>(bsp_node_count) * kModelBspNodeSize;
        cursor += static_cast<std::uint64_t>(facet_count) * kFacetTextureNameSize;
        if (cursor > p.size()) {
            return OdmError::HeaderTooSmall;
        }
    }
    out = cursor;
    return OdmError::None;
}

OdmError extract_decorations(const OdmMap& map, std::vector<OdmDecoration>& out) {
    out.clear();

    std::uint64_t start = 0;
    if (const OdmError e = model_geometry_end(map, start); e != OdmError::None) {
        return e;
    }

    const auto& p = map.payload;
    if (start + 4 > p.size()) {
        return OdmError::HeaderTooSmall;
    }
    io::ByteReader r{std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(p.data()), p.size()}};
    if (!r.seek(static_cast<std::size_t>(start))) {
        return OdmError::HeaderTooSmall;
    }
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok()) {
        return OdmError::HeaderTooSmall;
    }

    // A record array and a parallel name array, both `count` long.
    const std::uint64_t records = start + 4;
    const std::uint64_t names =
        records + static_cast<std::uint64_t>(count) * kDecorationRecordSize;
    const std::uint64_t end =
        names + static_cast<std::uint64_t>(count) * kDecorationNameSize;
    if (end > p.size()) {
        return OdmError::HeaderTooSmall;
    }

    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        OdmDecoration d;
        if (!r.seek(static_cast<std::size_t>(
                records + static_cast<std::uint64_t>(i) * kDecorationRecordSize))) {
            return OdmError::HeaderTooSmall;
        }
        d.kind = r.read_u32_le();
        d.x = r.read_i32_le();
        d.y = r.read_i32_le();
        d.z = r.read_i32_le();

        if (!r.seek(static_cast<std::size_t>(
                names + static_cast<std::uint64_t>(i) * kDecorationNameSize))) {
            return OdmError::HeaderTooSmall;
        }
        if (!r.read_fixed_string(kDecorationNameSize, d.name)) {
            return OdmError::HeaderTooSmall;
        }
        out.push_back(std::move(d));
    }
    if (!r.ok()) {
        return OdmError::HeaderTooSmall;
    }
    return OdmError::None;
}

}  // namespace starhaven::world
