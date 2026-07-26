// Tests for the MM6 ODM outdoor map parser.
//
// Fixtures are SYNTHETIC: a synthetic decompressed payload (a fixed header with
// known name/version/tileset fields plus a tail of zeros) is zlib-compressed
// and wrapped per docs/formats/odm.md. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include <zlib.h>

#include "core/world/odm_map.hpp"

using namespace starhaven::world;

namespace {

bool zlib_compress(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()), static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}
void put_u16_le(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    // Bounds-safe: small-payload fixtures write dim fields past their end.
    if (off + 1 < v.size()) {
        v[off] = static_cast<std::uint8_t>(x & 0xFF);
        v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    }
}
void put_fixed_string(std::vector<std::uint8_t>& v, std::size_t off, const std::string& s) {
    // Only write what fits in the payload; tests with deliberately small
    // payloads rely on this not writing out of bounds.
    for (std::size_t i = 0; i < 32 && off + i < v.size(); ++i) {
        v[off + i] = (i < s.size()) ? static_cast<std::uint8_t>(s[i]) : 0;
    }
}

// Build a valid ODM entry: an 8-byte wrapper around zlib-compressed synthetic
// header payload of `payload_size` bytes (header + zero tail).
std::vector<std::byte> make_odm_entry(std::size_t payload_size,
                                      const std::string& version = "MM6 Outdoor v1.11") {
    std::vector<std::uint8_t> payload(payload_size, 0);
    put_fixed_string(payload, 0x00, "blank");
    put_fixed_string(payload, 0x20, "default.odm");
    put_fixed_string(payload, 0x40, version);
    put_fixed_string(payload, 0x80, "grastyl");
    // dimension fields at 0xA0: distinct small values for verification
    for (int i = 0; i < 8; ++i) {
        put_u16_le(payload, 0xA0 + i * 2, static_cast<std::uint16_t>(100 + i));
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));

    std::vector<std::byte> entry(kWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kWrapperSize], compressed.data(), compressed.size());
    return entry;
}

}  // namespace

TEST_CASE("valid ODM header fields are parsed", "[odm]") {
    OdmMap m;
    auto entry = make_odm_entry(0x200);
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    REQUIRE(m.header.name == "blank");
    REQUIRE(m.header.file_name == "default.odm");
    REQUIRE(m.header.version == "MM6 Outdoor v1.11");
    REQUIRE(m.header.ground_name == "grastyl");
    // The fixture stamped u16 values 100..107 at 0xA0; these map to 4 tilesets.
    for (int i = 0; i < 4; ++i) {
        REQUIRE(m.header.tilesets[i].group == static_cast<std::int16_t>(100 + i * 2));
        REQUIRE(m.header.tilesets[i].offset == static_cast<std::int16_t>(101 + i * 2));
    }
    REQUIRE(m.payload.size() == 0x200);
}

TEST_CASE("truncated wrapper is rejected", "[odm]") {
    std::vector<std::byte> entry(4, std::byte{0});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::TooSmall);
}

TEST_CASE("corrupt zlib data is rejected", "[odm]") {
    std::vector<std::byte> entry(kWrapperSize + 16, std::byte{0xFF});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::InflateFailed);
}

TEST_CASE("decompressed size mismatch is rejected", "[odm]") {
    // Build a valid entry, then lie about the declared decompressed size, which
    // lives at offset 0x04 (the wrapper's second u32).
    auto entry = make_odm_entry(0x200);
    put_u32_le(entry, 0x04, 999999);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::SizeMismatch);
}

TEST_CASE("header too small after inflate is rejected", "[odm]") {
    // Payload shorter than the 0xB0-byte header.
    auto entry = make_odm_entry(0x40);
    OdmMap m;
    // The version check happens after the size check; either TooSmall-style
    // rejection is acceptable, but we expect HeaderTooSmall.
    REQUIRE(parse_odm(entry, m) == OdmError::HeaderTooSmall);
}

TEST_CASE("unsupported version is rejected", "[odm]") {
    auto entry = make_odm_entry(0x200, "BOGUS v9.99");
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::UnsupportedVersion);
}

TEST_CASE("declared size of zero is accepted (unknown size)", "[odm]") {
    auto entry = make_odm_entry(0x200);
    put_u32_le(entry, 0x04, 0);  // decompressed size at offset 4 = unknown
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
}

// --- terrain tests ---------------------------------------------------------

// Build an ODM entry large enough to hold both terrain grids, with known byte
// patterns stamped into the heightmap (0xB0) and tilemap (0x40B0) regions.
std::vector<std::byte> make_odm_entry_with_terrain(std::uint8_t height_fill,
                                                   std::uint8_t tile_fill) {
    constexpr std::size_t kPayloadSize = 0x80B0 + OdmTerrain::kGridBytes;  // both grids + tail
    std::vector<std::uint8_t> payload(kPayloadSize, 0);
    put_fixed_string(payload, 0x00, "blank");
    put_fixed_string(payload, 0x20, "default.odm");
    put_fixed_string(payload, 0x40, "MM6 Outdoor v1.11");
    put_fixed_string(payload, 0x80, "grastyl");
    // Fill the two terrain grids with distinct patterns.
    for (std::size_t i = 0; i < OdmTerrain::kGridBytes; ++i) {
        payload[0xB0 + i] = static_cast<std::uint8_t>(height_fill ^ (i & 0x0F));
        payload[0x40B0 + i] = static_cast<std::uint8_t>(tile_fill ^ (i & 0x0F));
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kWrapperSize], compressed.data(), compressed.size());
    return entry;
}

TEST_CASE("terrain heightmap and tilemap are extracted", "[odm]") {
    auto entry = make_odm_entry_with_terrain(/*height*/ 0x40, /*tile*/ 0xC0);
    OdmMap m;
    OdmTerrain t;
    REQUIRE(parse_odm_terrain(entry, m, t) == OdmError::None);

    REQUIRE(t.heightmap.size() == OdmTerrain::kGridBytes);
    REQUIRE(t.tilemap.size() == OdmTerrain::kGridBytes);
    // Verify a few cells against the stamped pattern.
    for (std::size_t i : {0u, 1u, 127u, 128u, 16383u}) {
        REQUIRE(t.heightmap[i] == static_cast<std::uint8_t>(0x40 ^ (i & 0x0F)));
        REQUIRE(t.tilemap[i] == static_cast<std::uint8_t>(0xC0 ^ (i & 0x0F)));
    }
}

TEST_CASE("terrain indexing matches row-major layout", "[odm]") {
    auto entry = make_odm_entry_with_terrain(0x10, 0x20);
    OdmMap m;
    OdmTerrain t;
    REQUIRE(parse_odm_terrain(entry, m, t) == OdmError::None);
    // grid index for (x,y) = y*128 + x
    const std::size_t x = 5, y = 7;
    const std::size_t idx = y * OdmTerrain::kGridDim + x;
    REQUIRE(t.heightmap[idx] == static_cast<std::uint8_t>(0x10 ^ (idx & 0x0F)));
}

TEST_CASE("terrain extraction rejects a too-short payload", "[odm]") {
    // Payload too small to hold the heightmap (needs 0xB0 + 16384 = 0x40B0).
    auto entry = make_odm_entry(0x200);
    OdmMap m;
    OdmTerrain t;
    REQUIRE(parse_odm_terrain(entry, m, t) == OdmError::HeaderTooSmall);
}

// --- model tests -----------------------------------------------------------

// Build an ODM entry whose payload is large enough to hold `count` model
// records after the terrain grids, with known name/position/bbox in model 0.
std::vector<std::byte> make_odm_entry_with_models(std::uint32_t count) {
    const std::size_t payload_size = kModelCountOffset + 4 + count * kModelRecordSize + 8;
    std::vector<std::uint8_t> payload(payload_size, 0);
    put_fixed_string(payload, 0x00, "blank");
    put_fixed_string(payload, 0x20, "default.odm");
    put_fixed_string(payload, 0x40, "MM6 Outdoor v1.11");
    put_fixed_string(payload, 0x80, "grastyl");

    // Model count at 0xC0B0.
    payload[0xC0B0] = static_cast<std::uint8_t>(count & 0xFF);
    payload[0xC0B1] = static_cast<std::uint8_t>((count >> 8) & 0xFF);
    payload[0xC0B2] = static_cast<std::uint8_t>((count >> 16) & 0xFF);
    payload[0xC0B3] = static_cast<std::uint8_t>((count >> 24) & 0xFF);

    if (count > 0) {
        const std::size_t m0 = kModelCountOffset + 4;
        put_fixed_string(payload, m0, "testmodel");
        // pos at 0x70: (100, 200, 300)
        auto put_i32 = [&](std::size_t o, std::int32_t v) {
            payload[o] = static_cast<std::uint8_t>(v & 0xFF);
            payload[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            payload[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
            payload[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        };
        put_i32(m0 + 0x70, 100);
        put_i32(m0 + 0x74, 200);
        put_i32(m0 + 0x78, 300);
        put_i32(m0 + 0x7C, -50);  // min x
        put_i32(m0 + 0x88, 50);   // max x
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kWrapperSize], compressed.data(), compressed.size());
    return entry;
}

TEST_CASE("zero models is accepted", "[odm]") {
    auto entry = make_odm_entry_with_models(0);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModel> models;
    REQUIRE(extract_models(m, models) == OdmError::None);
    REQUIRE(models.empty());
}

TEST_CASE("one model decodes name, position, and bounding box", "[odm]") {
    auto entry = make_odm_entry_with_models(1);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModel> models;
    REQUIRE(extract_models(m, models) == OdmError::None);
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].name == "testmodel");
    REQUIRE(models[0].pos_x == 100);
    REQUIRE(models[0].pos_y == 200);
    REQUIRE(models[0].pos_z == 300);
    REQUIRE(models[0].min_x == -50);
    REQUIRE(models[0].max_x == 50);
}

TEST_CASE("model array past EOF is rejected", "[odm]") {
    auto entry = make_odm_entry_with_models(1);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    // Lie about the count to push the array past EOF.
    m.payload[kModelCountOffset] = 0xFF;
    m.payload[kModelCountOffset + 1] = 0xFF;
    m.payload[kModelCountOffset + 2] = 0xFF;
    m.payload[kModelCountOffset + 3] = 0x7F;
    std::vector<OdmModel> models;
    REQUIRE(extract_models(m, models) == OdmError::HeaderTooSmall);
}

TEST_CASE("payload too short for the model count is rejected", "[odm]") {
    OdmMap m;
    m.payload.resize(0x10, std::uint8_t{0});  // far too short
    std::vector<OdmModel> models;
    REQUIRE(extract_models(m, models) == OdmError::HeaderTooSmall);
}

// --- model vertex tests ----------------------------------------------------

// Build an ODM entry with one model that has `vcount` vertices (12 bytes each)
// of known coordinates stamped into the geometry section.
std::vector<std::byte> make_odm_entry_with_vertices(std::uint32_t vcount) {
    const std::size_t geo_start = kModelCountOffset + 4 + 1 * kModelRecordSize;
    const std::size_t payload_size = geo_start + vcount * kModelVertexSize + 8;
    std::vector<std::uint8_t> payload(payload_size, 0);
    put_fixed_string(payload, 0x00, "blank");
    put_fixed_string(payload, 0x20, "default.odm");
    put_fixed_string(payload, 0x40, "MM6 Outdoor v1.11");
    put_fixed_string(payload, 0x80, "grastyl");

    // One model.
    payload[0xC0B0] = 1;
    const std::size_t m0 = kModelCountOffset + 4;
    put_fixed_string(payload, m0, "testmodel");
    // vertex_count at +0x44
    auto put_u32 = [&](std::size_t o, std::uint32_t v) {
        payload[o] = static_cast<std::uint8_t>(v & 0xFF);
        payload[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        payload[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        payload[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    put_u32(m0 + 0x44, vcount);
    // Stamp vertices: (i*10, i*10+1, i*10+2) for i in 0..vcount-1.
    auto put_i32 = [&](std::size_t o, std::int32_t v) {
        put_u32(o, static_cast<std::uint32_t>(v));
    };
    for (std::uint32_t i = 0; i < vcount; ++i) {
        const std::size_t vo = geo_start + i * kModelVertexSize;
        put_i32(vo, static_cast<std::int32_t>(i * 10));
        put_i32(vo + 4, static_cast<std::int32_t>(i * 10 + 1));
        put_i32(vo + 8, static_cast<std::int32_t>(i * 10 + 2));
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kWrapperSize], compressed.data(), compressed.size());
    return entry;
}

TEST_CASE("first model vertices are extracted with correct coordinates", "[odm]") {
    auto entry = make_odm_entry_with_vertices(3);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelVertex> verts;
    REQUIRE(extract_first_model_vertices(m, verts) == OdmError::None);
    REQUIRE(verts.size() == 3);
    REQUIRE(verts[0].x == 0);
    REQUIRE(verts[0].y == 1);
    REQUIRE(verts[0].z == 2);
    REQUIRE(verts[1].x == 10);
    REQUIRE(verts[1].y == 11);
    REQUIRE(verts[1].z == 12);
    REQUIRE(verts[2].x == 20);
    REQUIRE(verts[2].y == 21);
    REQUIRE(verts[2].z == 22);
}

TEST_CASE("model with zero vertices is accepted", "[odm]") {
    auto entry = make_odm_entry_with_vertices(0);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelVertex> verts;
    REQUIRE(extract_first_model_vertices(m, verts) == OdmError::None);
    REQUIRE(verts.empty());
}

TEST_CASE("model_vertex_count reads the +0x44 field", "[odm]") {
    auto entry = make_odm_entry_with_vertices(5);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::uint32_t c = 999;
    REQUIRE(model_vertex_count(m, 0, c));
    REQUIRE(c == 5);
    REQUIRE_FALSE(model_vertex_count(m, 1, c));  // only one model
}

TEST_CASE("vertex array past EOF is rejected", "[odm]") {
    auto entry = make_odm_entry_with_vertices(3);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    // Lie about the vertex count to push the array past EOF.
    const std::size_t m0 = kModelCountOffset + 4;
    m.payload[m0 + 0x44] = 0xFF;
    m.payload[m0 + 0x45] = 0xFF;
    m.payload[m0 + 0x46] = 0xFF;
    m.payload[m0 + 0x47] = 0x7F;
    std::vector<OdmModelVertex> verts;
    REQUIRE(extract_first_model_vertices(m, verts) == OdmError::HeaderTooSmall);
}

// --- model mesh (facet) tests ----------------------------------------------

// One model's geometry description for the fixture builder below.
struct MeshSpec {
    std::uint32_t vertex_count = 0;
    std::uint32_t facet_count = 0;
    std::uint32_t bsp_node_count = 0;  // real maps use 0; nonzero shifts names
    std::uint8_t facet_vertices = 4;   // polygon size of every facet
};

// Build an ODM entry whose geometry stream holds one block per spec, laid out
// as verts | facets | ordering | bsp | names (docs/formats/odm-model-facets.md).
//
// Every value is derived from the model/facet index so the test can assert on
// exact numbers and prove that later models are reached at the right offsets.
std::vector<std::byte> make_odm_entry_with_meshes(const std::vector<MeshSpec>& specs) {
    const std::size_t geo_start = kModelCountOffset + 4 + specs.size() * kModelRecordSize;

    std::size_t geo_bytes = 0;
    for (const auto& s : specs) {
        geo_bytes += s.vertex_count * kModelVertexSize + s.facet_count * kModelFacetSize +
                     s.facet_count * kFacetOrderingEntrySize +
                     s.bsp_node_count * kModelBspNodeSize + s.facet_count * kFacetTextureNameSize;
    }
    std::vector<std::uint8_t> payload(geo_start + geo_bytes, 0);
    put_fixed_string(payload, 0x00, "blank");
    put_fixed_string(payload, 0x20, "default.odm");
    put_fixed_string(payload, 0x40, "MM6 Outdoor v1.11");
    put_fixed_string(payload, 0x80, "grastyl");

    auto put_u32 = [&](std::size_t o, std::uint32_t v) {
        payload[o] = static_cast<std::uint8_t>(v & 0xFF);
        payload[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        payload[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        payload[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    auto put_u16 = [&](std::size_t o, std::uint16_t v) {
        payload[o] = static_cast<std::uint8_t>(v & 0xFF);
        payload[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    };
    auto put_i32 = [&](std::size_t o, std::int32_t v) {
        put_u32(o, static_cast<std::uint32_t>(v));
    };

    put_u32(kModelCountOffset, static_cast<std::uint32_t>(specs.size()));

    std::size_t cursor = geo_start;
    for (std::size_t mi = 0; mi < specs.size(); ++mi) {
        const MeshSpec& s = specs[mi];
        const std::size_t rec = kModelCountOffset + 4 + mi * kModelRecordSize;
        put_fixed_string(payload, rec, "model" + std::to_string(mi));
        put_u32(rec + 0x44, s.vertex_count);
        put_u32(rec + 0x4C, s.facet_count);
        put_u32(rec + 0x5C, s.bsp_node_count);

        // Vertices: (mi*1000 + i, +1, +2).
        for (std::uint32_t i = 0; i < s.vertex_count; ++i) {
            const std::size_t o = cursor + i * kModelVertexSize;
            put_i32(o, static_cast<std::int32_t>(mi * 1000 + i));
            put_i32(o + 4, static_cast<std::int32_t>(mi * 1000 + i + 1));
            put_i32(o + 8, static_cast<std::int32_t>(mi * 1000 + i + 2));
        }
        cursor += s.vertex_count * kModelVertexSize;

        const std::size_t facets_off = cursor;
        for (std::uint32_t f = 0; f < s.facet_count; ++f) {
            const std::size_t o = facets_off + f * kModelFacetSize;
            put_i32(o, 0);             // normal x
            put_i32(o + 0x04, 0);      // normal y
            put_i32(o + 0x08, 65536);  // normal z = 1.0 in 16.16
            put_i32(o + 0x0C, -65536 * static_cast<std::int32_t>(f + 1));
            put_u32(o + 0x1C, 0x100u + f);          // attributes
            payload[o + 0x12E] = s.facet_vertices;  // polygon size
            for (std::uint8_t k = 0; k < s.facet_vertices; ++k) {
                // Cycle through the model's own vertices.
                put_u16(o + 0x20 + k * 2u, static_cast<std::uint16_t>((f + k) % s.vertex_count));
                put_u16(o + 0x48 + k * 2u, static_cast<std::uint16_t>(10 * k));
                put_u16(o + 0x70 + k * 2u, static_cast<std::uint16_t>(20 * k));
            }
        }
        cursor += s.facet_count * kModelFacetSize;
        cursor += s.facet_count * kFacetOrderingEntrySize;
        cursor += s.bsp_node_count * kModelBspNodeSize;

        // Texture names: "texMxF", NUL-padded to 10 bytes.
        for (std::uint32_t f = 0; f < s.facet_count; ++f) {
            const std::string n = "tex" + std::to_string(mi) + "x" + std::to_string(f);
            for (std::size_t i = 0; i < kFacetTextureNameSize; ++i) {
                payload[cursor + f * kFacetTextureNameSize + i] =
                    (i < n.size()) ? static_cast<std::uint8_t>(n[i]) : 0;
            }
        }
        cursor += s.facet_count * kFacetTextureNameSize;
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> entry(kWrapperSize + compressed.size());
    put_u32_le(entry, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(entry, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kWrapperSize], compressed.data(), compressed.size());
    return entry;
}

// Build an entry whose model geometry is followed by a decoration array: a
// count, then 28-byte records, then a parallel array of 32-byte names.
std::vector<std::byte> make_odm_entry_with_decorations(
    const std::vector<MeshSpec>& specs,
    const std::vector<
        std::tuple<std::uint32_t, std::int32_t, std::int32_t, std::int32_t, std::string>>& decos) {
    auto entry = make_odm_entry_with_meshes(specs);
    // Recover the payload so the decorations can be appended to it.
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<std::uint8_t> payload = m.payload;

    auto push_u32 = [&](std::uint32_t v) {
        payload.push_back(static_cast<std::uint8_t>(v & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };

    push_u32(static_cast<std::uint32_t>(decos.size()));
    for (const auto& [kind, x, y, z, name] : decos) {
        const std::size_t base = payload.size();
        payload.resize(base + kDecorationRecordSize, 0);
        auto put = [&](std::size_t off, std::uint32_t v) {
            payload[base + off + 0] = static_cast<std::uint8_t>(v & 0xFF);
            payload[base + off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            payload[base + off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
            payload[base + off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        };
        put(0x00, kind);
        put(0x04, static_cast<std::uint32_t>(x));
        put(0x08, static_cast<std::uint32_t>(y));
        put(0x0C, static_cast<std::uint32_t>(z));
    }
    for (const auto& [kind, x, y, z, name] : decos) {
        (void)kind;
        (void)x;
        (void)y;
        (void)z;
        const std::size_t base = payload.size();
        payload.resize(base + kDecorationNameSize, 0);
        for (std::size_t i = 0; i < name.size() && i + 1 < kDecorationNameSize; ++i) {
            payload[base + i] = static_cast<std::uint8_t>(name[i]);
        }
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> out(kWrapperSize + compressed.size());
    put_u32_le(out, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(out, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&out[kWrapperSize], compressed.data(), compressed.size());
    return out;
}

TEST_CASE("model meshes decode vertices, facets, and texture names", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{4, 2, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::None);
    REQUIRE(meshes.size() == 1);
    REQUIRE(meshes[0].vertices.size() == 4);
    REQUIRE(meshes[0].facets.size() == 2);

    REQUIRE(meshes[0].vertices[2].x == 2);
    REQUIRE(meshes[0].vertices[2].y == 3);
    REQUIRE(meshes[0].vertices[2].z == 4);

    const OdmModelFacet& f0 = meshes[0].facets[0];
    REQUIRE(f0.vertex_count == 4);
    REQUIRE(f0.normal_z == 65536);
    REQUIRE(f0.nz() == 1.0f);
    REQUIRE(f0.plane_distance == -65536);
    REQUIRE(f0.attributes == 0x100);
    REQUIRE(f0.vertex_ids[0] == 0);
    REQUIRE(f0.vertex_ids[3] == 3);
    REQUIRE(f0.u[2] == 20);
    REQUIRE(f0.v[2] == 40);
    REQUIRE(f0.texture_name == "tex0x0");
    REQUIRE(meshes[0].facets[1].texture_name == "tex0x1");
    REQUIRE(meshes[0].facets[1].attributes == 0x101);
}

TEST_CASE("later models are reached past the preceding geometry", "[odm]") {
    // The point of this slice: model 1 and 2 sit behind model 0's facets,
    // ordering array and texture names, with no offset table to shortcut it.
    auto entry = make_odm_entry_with_meshes({{4, 3, 0, 4}, {5, 2, 0, 3}, {6, 1, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::None);
    REQUIRE(meshes.size() == 3);

    REQUIRE(meshes[1].vertices.size() == 5);
    REQUIRE(meshes[1].facets.size() == 2);
    REQUIRE(meshes[1].vertices[0].x == 1000);  // model index encoded in the data
    REQUIRE(meshes[1].facets[0].vertex_count == 3);
    REQUIRE(meshes[1].facets[1].texture_name == "tex1x1");

    REQUIRE(meshes[2].vertices.size() == 6);
    REQUIRE(meshes[2].vertices[0].x == 2000);
    REQUIRE(meshes[2].facets[0].texture_name == "tex2x0");
}

TEST_CASE("BSP nodes between facets and names are skipped", "[odm]") {
    // No shipped map uses them, but the loader reserves 8 bytes per node
    // between the ordering array and the texture names.
    auto entry = make_odm_entry_with_meshes({{4, 2, 7, 4}, {4, 1, 0, 3}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::None);
    REQUIRE(meshes.size() == 2);
    REQUIRE(meshes[0].facets[0].texture_name == "tex0x0");
    REQUIRE(meshes[1].facets[0].texture_name == "tex1x0");
    REQUIRE(meshes[1].vertices[0].x == 1000);
}

TEST_CASE("zero models yields no meshes", "[odm]") {
    auto entry = make_odm_entry_with_meshes({});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::None);
    REQUIRE(meshes.empty());
}

TEST_CASE("a model with no facets is accepted", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{3, 0, 0, 4}, {4, 1, 0, 3}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::None);
    REQUIRE(meshes[0].vertices.size() == 3);
    REQUIRE(meshes[0].facets.empty());
    REQUIRE(meshes[1].facets.size() == 1);  // the next block still lines up
}

TEST_CASE("a facet claiming more vertices than the record holds is rejected", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{4, 1, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    const std::size_t facet0 = kModelCountOffset + 4 + 1 * kModelRecordSize + 4 * kModelVertexSize;
    m.payload[facet0 + 0x12E] = static_cast<std::uint8_t>(kFacetMaxVertices + 1);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::HeaderTooSmall);
}

TEST_CASE("a facet referencing a vertex the model lacks is rejected", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{4, 1, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    const std::size_t facet0 = kModelCountOffset + 4 + 1 * kModelRecordSize + 4 * kModelVertexSize;
    m.payload[facet0 + 0x20] = 99;  // vertex id 99, but the model has 4
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::HeaderTooSmall);
}

TEST_CASE("a geometry stream that runs past the payload is rejected", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{4, 1, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    // Drop the texture-name block off the end.
    m.payload.resize(m.payload.size() - kFacetTextureNameSize);
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::HeaderTooSmall);
}

TEST_CASE("a hostile facet count cannot wrap the size arithmetic", "[odm]") {
    auto entry = make_odm_entry_with_meshes({{4, 1, 0, 4}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    const std::size_t rec = kModelCountOffset + 4;
    m.payload[rec + 0x4C] = 0xFF;
    m.payload[rec + 0x4D] = 0xFF;
    m.payload[rec + 0x4E] = 0xFF;
    m.payload[rec + 0x4F] = 0xFF;
    std::vector<OdmModelMesh> meshes;
    REQUIRE(extract_model_meshes(m, meshes) == OdmError::HeaderTooSmall);
}

TEST_CASE("decorations decode after the model geometry", "[odm]") {
    // The array sits at a computable offset: right after the last model's
    // geometry, with its own count.
    auto entry = make_odm_entry_with_decorations({{4, 2, 0, 4}, {5, 1, 0, 3}},
                                                 {{39, 3232, 9072, 320, "tree27"},
                                                  {40, -1000, 500, -64, "tree28"},
                                                  {2, 0, 0, 0, "Party Start"}});

    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmDecoration> decos;
    REQUIRE(extract_decorations(m, decos) == OdmError::None);
    REQUIRE(decos.size() == 3);
    REQUIRE(decos[0].kind == 39);
    REQUIRE(decos[0].x == 3232);
    REQUIRE(decos[0].y == 9072);
    REQUIRE(decos[0].z == 320);
    REQUIRE(decos[0].name == "tree27");
    REQUIRE(decos[1].x == -1000);  // negative coordinates survive
    REQUIRE(decos[1].z == -64);
    REQUIRE(decos[2].name == "Party Start");
}

TEST_CASE("the geometry end is where the decorations begin", "[odm]") {
    auto entry = make_odm_entry_with_decorations({{4, 2, 0, 4}}, {{1, 0, 0, 0, "tree27"}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::uint64_t end = 0;
    REQUIRE(model_geometry_end(m, end) == OdmError::None);
    // count + one record + one name accounts for the rest of the payload.
    REQUIRE(end + 4 + kDecorationRecordSize + kDecorationNameSize == m.payload.size());
}

TEST_CASE("a decoration array running past the payload is rejected", "[odm]") {
    auto entry = make_odm_entry_with_decorations({{4, 2, 0, 4}}, {{1, 0, 0, 0, "tree27"}});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::uint64_t end = 0;
    REQUIRE(model_geometry_end(m, end) == OdmError::None);
    // Claim far more decorations than the payload can hold.
    m.payload[static_cast<std::size_t>(end)] = 0xFF;
    m.payload[static_cast<std::size_t>(end) + 1] = 0xFF;
    std::vector<OdmDecoration> decos;
    REQUIRE(extract_decorations(m, decos) == OdmError::HeaderTooSmall);
}

TEST_CASE("a map with no decorations decodes as empty", "[odm]") {
    auto entry = make_odm_entry_with_decorations({{4, 2, 0, 4}}, {});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<OdmDecoration> decos;
    REQUIRE(extract_decorations(m, decos) == OdmError::None);
    REQUIRE(decos.empty());
}

namespace {

std::vector<std::byte> make_odm_entry_with_tail(
    const std::vector<
        std::tuple<std::uint32_t, std::int32_t, std::int32_t, std::int32_t, std::string>>& decos,
    int at_tile, const std::vector<std::uint16_t>& pids, const std::vector<OdmSpawnPoint>& spawns) {
    auto entry = make_odm_entry_with_decorations({{4, 2, 0, 4}}, decos);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    std::vector<std::uint8_t> payload = m.payload;

    auto push_u16 = [&](std::uint16_t v) {
        payload.push_back(static_cast<std::uint8_t>(v & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    auto push_u32 = [&](std::uint32_t v) {
        payload.push_back(static_cast<std::uint8_t>(v & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };

    constexpr int kTiles = OdmTileIndex::kDim * OdmTileIndex::kDim;
    std::vector<std::uint16_t> entries;
    std::vector<std::uint32_t> starts(kTiles, 0);
    for (int t = 0; t < kTiles; ++t) {
        starts[static_cast<std::size_t>(t)] = static_cast<std::uint32_t>(entries.size());
        if (t == at_tile) {
            entries.insert(entries.end(), pids.begin(), pids.end());
        }
        entries.push_back(0);  // the terminator every tile's run ends with
    }

    push_u32(static_cast<std::uint32_t>(entries.size()));
    for (const std::uint16_t e : entries) {
        push_u16(e);
    }
    for (const std::uint32_t st : starts) {
        push_u32(st);
    }
    push_u32(static_cast<std::uint32_t>(spawns.size()));
    for (const auto& s : spawns) {
        push_u32(static_cast<std::uint32_t>(s.x));
        push_u32(static_cast<std::uint32_t>(s.y));
        push_u32(static_cast<std::uint32_t>(s.z));
        push_u16(s.radius);
        push_u16(s.kind);
        push_u32(s.index);
    }

    std::vector<std::uint8_t> compressed;
    REQUIRE(zlib_compress(payload, compressed));
    std::vector<std::byte> out(kWrapperSize + compressed.size());
    put_u32_le(out, 0x00, static_cast<std::uint32_t>(payload.size()));
    put_u32_le(out, 0x04, static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&out[kWrapperSize], compressed.data(), compressed.size());
    return out;
}

constexpr int kMiddleTile = 64 * OdmTileIndex::kDim + 64;

}  // namespace

TEST_CASE("the tile index lists what stands near a tile", "[odm]") {
    auto entry = make_odm_entry_with_tail({{1, 0, 0, 0, "tree27"}, {1, 0, 0, 0, "tree28"}},
                                          kMiddleTile, {5, 13}, {});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);

    OdmTileIndex index;
    REQUIRE(extract_tile_index(m, index) == OdmError::None);

    const auto run = index.at(64, 64);
    REQUIRE(run.size() == 2);
    REQUIRE(pid_type(run[0]) == kPidDecoration);
    REQUIRE(pid_id(run[0]) == 0);
    REQUIRE(pid_id(run[1]) == 1);
}

TEST_CASE("the terminator is not one of the identifiers", "[odm]") {
    // Every tile's run ends with a zero, including the empty ones. Returning it
    // would make each tile appear to hold a decoration with id 0.
    auto entry = make_odm_entry_with_tail({{1, 0, 0, 0, "tree27"}}, kMiddleTile, {5}, {});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    OdmTileIndex index;
    REQUIRE(extract_tile_index(m, index) == OdmError::None);

    REQUIRE(index.at(64, 64).size() == 1);
    REQUIRE(index.at(63, 64).empty());
    REQUIRE(index.at(0, 0).empty());
}

TEST_CASE("a tile outside the grid has nothing on it", "[odm]") {
    auto entry = make_odm_entry_with_tail({{1, 0, 0, 0, "tree27"}}, kMiddleTile, {5}, {});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);
    OdmTileIndex index;
    REQUIRE(extract_tile_index(m, index) == OdmError::None);

    REQUIRE(index.at(-1, 0).empty());
    REQUIRE(index.at(0, -1).empty());
    REQUIRE(index.at(OdmTileIndex::kDim, 0).empty());
    REQUIRE(index.at(0, OdmTileIndex::kDim).empty());
}

TEST_CASE("world positions map onto the grid", "[odm]") {
    // The grid is centred and its rows run against world y, so the tile a
    // position falls in is not the same arithmetic on both axes.
    REQUIRE(OdmTileIndex::tile_x_of(0.0f) == 64);
    REQUIRE(OdmTileIndex::tile_y_of(0.0f) == 64);
    REQUIRE(OdmTileIndex::tile_x_of(511.0f) == 64);
    REQUIRE(OdmTileIndex::tile_x_of(512.0f) == 65);
    REQUIRE(OdmTileIndex::tile_x_of(-1.0f) == 63);
    REQUIRE(OdmTileIndex::tile_y_of(512.0f) == 63);
    REQUIRE(OdmTileIndex::tile_y_of(-1.0f) == 65);
    // Sweet Water's first decoration, which the shipped index puts on tile 47.
    REQUIRE(OdmTileIndex::tile_x_of(3232.0f) == 70);
    REQUIRE(OdmTileIndex::tile_y_of(9072.0f) == 47);
}

TEST_CASE("spawn points decode after the tile index", "[odm]") {
    const std::vector<OdmSpawnPoint> spawns{{6992, 11824, 0, 32, 3, 2}, {-1000, 500, 300, 0, 3, 1}};
    auto entry = make_odm_entry_with_tail({{1, 0, 0, 0, "tree27"}}, kMiddleTile, {5}, spawns);
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);

    std::vector<OdmSpawnPoint> out;
    REQUIRE(extract_spawn_points(m, out) == OdmError::None);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].x == 6992);
    REQUIRE(out[0].y == 11824);
    REQUIRE(out[0].radius == 32);
    REQUIRE(out[0].kind == 3);
    REQUIRE(out[0].index == 2);
    REQUIRE(out[1].x == -1000);
    REQUIRE(out[1].z == 300);
}

TEST_CASE("a tail that runs past the payload is rejected", "[odm]") {
    auto entry = make_odm_entry_with_tail({{1, 0, 0, 0, "tree27"}}, kMiddleTile, {5}, {});
    OdmMap m;
    REQUIRE(parse_odm(entry, m) == OdmError::None);

    // Truncating the payload leaves the declared index longer than what is
    // there, which has to fail rather than read past the end.
    m.payload.resize(m.payload.size() - 16);
    OdmTileIndex index;
    REQUIRE(extract_tile_index(m, index) == OdmError::HeaderTooSmall);
    std::vector<OdmSpawnPoint> spawns;
    REQUIRE(extract_spawn_points(m, spawns) == OdmError::HeaderTooSmall);
}
