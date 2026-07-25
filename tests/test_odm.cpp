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

using namespace openmm6::world;

namespace {

bool zlib_compress(const std::vector<std::uint8_t>& src,
                   std::vector<std::uint8_t>& dst) {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    dst.resize(bound);
    uLongf len = bound;
    if (compress2(reinterpret_cast<Bytef*>(dst.data()), &len,
                  reinterpret_cast<const Bytef*>(src.data()),
                  static_cast<uLong>(src.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }
    dst.resize(len);
    return true;
}

void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}
void put_u16_le(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    // Bounds-safe: small-payload fixtures write dim fields past their end.
    if (off + 1 < v.size()) {
        v[off]     = static_cast<std::uint8_t>(x & 0xFF);
        v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    }
}
void put_fixed_string(std::vector<std::uint8_t>& v, std::size_t off,
                      const std::string& s) {
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
    const std::size_t payload_size =
        kModelCountOffset + 4 + count * kModelRecordSize + 8;
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
        put_i32(m0 + 0x7C, -50);   // min x
        put_i32(m0 + 0x88, 50);    // max x
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
    const std::size_t geo_start =
        kModelCountOffset + 4 + 1 * kModelRecordSize;
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
    REQUIRE(verts[0].x == 0);  REQUIRE(verts[0].y == 1);  REQUIRE(verts[0].z == 2);
    REQUIRE(verts[1].x == 10); REQUIRE(verts[1].y == 11); REQUIRE(verts[1].z == 12);
    REQUIRE(verts[2].x == 20); REQUIRE(verts[2].y == 21); REQUIRE(verts[2].z == 22);
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
