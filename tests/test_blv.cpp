// Tests for the MM6 .blv indoor map parser.
//
// Fixtures are SYNTHETIC: a payload is assembled here from the layout in
// docs/formats/blv.md, then zlib-compressed and wrapped. No bytes from the
// game are involved.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <zlib.h>

#include "core/world/blv_map.hpp"

using namespace starhaven::world;

namespace {

struct FaceSpec {
    std::vector<std::uint16_t> ids;
    std::uint32_t attributes = 0;
    std::string texture;
    // Plane, in 16.16. The default is the z = 0 plane, which the fixture's
    // vertices lie on.
    std::int32_t nx = 0, ny = 0, nz = 65536, d = 0;
};

void put_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    v[off + 0] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::uint8_t>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::uint8_t>((x >> 24) & 0xFF);
}

void push_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
}

void push_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    push_u16(v, static_cast<std::uint16_t>(x & 0xFFFF));
    push_u16(v, static_cast<std::uint16_t>((x >> 16) & 0xFFFF));
}

// Build a decompressed payload with the given vertices and faces.
std::vector<std::uint8_t>
make_payload(const std::vector<std::array<std::int16_t, 3>>& vertices,
             const std::vector<FaceSpec>& faces, const std::string& name = "Test Level",
             const std::string& name2 = "test",
             const std::vector<std::pair<std::uint16_t, std::uint16_t>>& extras = {}) {
    std::vector<std::uint8_t> p(kBlvHeaderSize, 0);
    put_u32(p, 0x00, 1);
    for (std::size_t i = 0; i < name.size(); ++i)
        p[0x04 + i] = static_cast<std::uint8_t>(name[i]);
    for (std::size_t i = 0; i < name2.size(); ++i)
        p[0x50 + i] = static_cast<std::uint8_t>(name2[i]);

    // The index block is six u16 arrays of (n + 1) entries per face.
    std::uint32_t index_bytes = 0;
    for (const auto& f : faces) {
        index_bytes += static_cast<std::uint32_t>((f.ids.size() + 1) * 2 * kBlvFaceArrayCount);
    }
    put_u32(p, 0x68, index_bytes);
    put_u32(p, 0x6C, 111);
    put_u32(p, 0x70, 222);
    put_u32(p, 0x74, 333);

    put_u32(p, kBlvVertexCountOffset, static_cast<std::uint32_t>(vertices.size()));
    for (const auto& v : vertices) {
        push_u16(p, static_cast<std::uint16_t>(v[0]));
        push_u16(p, static_cast<std::uint16_t>(v[1]));
        push_u16(p, static_cast<std::uint16_t>(v[2]));
    }

    push_u32(p, static_cast<std::uint32_t>(faces.size()));
    for (const auto& f : faces) {
        const std::size_t base = p.size();
        p.resize(base + kBlvFaceSize, 0);
        put_u32(p, base + 0x00, static_cast<std::uint32_t>(f.nx));
        put_u32(p, base + 0x04, static_cast<std::uint32_t>(f.ny));
        put_u32(p, base + 0x08, static_cast<std::uint32_t>(f.nz));
        put_u32(p, base + 0x0C, static_cast<std::uint32_t>(f.d));
        put_u32(p, base + 0x1C, f.attributes);
        p[base + 0x4D] = static_cast<std::uint8_t>(f.ids.size());
    }

    // Index arrays: six per face, the first being the vertex ids with a
    // closing copy of the first entry.
    for (const auto& f : faces) {
        for (std::uint16_t id : f.ids)
            push_u16(p, id);
        push_u16(p, f.ids.empty() ? 0 : f.ids.front());
        for (std::size_t a = 1; a < kBlvFaceArrayCount; ++a) {
            for (std::size_t k = 0; k < f.ids.size() + 1; ++k) {
                push_u16(p, static_cast<std::uint16_t>(a * 100 + k));
            }
        }
    }

    for (const auto& f : faces) {
        for (std::size_t i = 0; i < kBlvTextureNameSize; ++i) {
            p.push_back(i < f.texture.size() ? static_cast<std::uint8_t>(f.texture[i])
                                             : std::uint8_t{0});
        }
    }

    // Face extras: a count, then 36-byte records naming a face. The u16 at
    // +0x0e must be 0xffff, which is the invariant the parser checks.
    push_u32(p, static_cast<std::uint32_t>(extras.size()));
    for (const auto& [face_index, tag] : extras) {
        const std::size_t base = p.size();
        p.resize(base + kBlvFaceExtraSize, 0);
        auto put_u16 = [&](std::size_t off, std::uint16_t v) {
            p[base + off] = static_cast<std::uint8_t>(v & 0xFF);
            p[base + off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };
        put_u16(0x0C, face_index);
        put_u16(0x0E, 0xFFFF);
        put_u16(0x14, tag);
        put_u16(0x16, static_cast<std::uint16_t>(tag + 1));
        put_u16(0x1A, static_cast<std::uint16_t>(tag + 2));
    }
    // One 10-byte name per extra, as faces have.
    for (std::size_t i = 0; i < extras.size(); ++i) {
        const std::string nm = (i == 0) ? std::string("extraA") : std::string();
        const std::size_t base = p.size();
        p.resize(base + kBlvFaceExtraNameSize, 0);
        for (std::size_t k = 0; k < nm.size() && k + 1 < kBlvFaceExtraNameSize; ++k) {
            p[base + k] = static_cast<std::uint8_t>(nm[k]);
        }
    }
    return p;
}

// Wrap a payload: 8-byte header then a zlib stream.
std::vector<std::byte> wrap(const std::vector<std::uint8_t>& payload, bool corrupt_checksum = false,
                            std::uint32_t declared_override = 0) {
    uLongf bound = compressBound(static_cast<uLong>(payload.size()));
    std::vector<std::uint8_t> compressed(bound);
    uLongf len = bound;
    REQUIRE(compress2(compressed.data(), &len, payload.data(), static_cast<uLong>(payload.size()),
                      Z_DEFAULT_COMPRESSION) == Z_OK);
    compressed.resize(len);
    if (corrupt_checksum) {
        // The Adler-32 trailer is the last four bytes.
        compressed[compressed.size() - 1] ^= 0xFF;
    }

    std::vector<std::byte> entry(kBlvWrapperSize + compressed.size());
    auto put = [&](std::size_t off, std::uint32_t x) {
        entry[off + 0] = static_cast<std::byte>(x & 0xFF);
        entry[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
        entry[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
        entry[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
    };
    put(0x00, static_cast<std::uint32_t>(compressed.size()));
    put(0x04,
        declared_override != 0 ? declared_override : static_cast<std::uint32_t>(payload.size()));
    std::memcpy(&entry[kBlvWrapperSize], compressed.data(), compressed.size());
    return entry;
}

// Append decoration records to a payload: 32 bytes each, name then flags,
// position and facing.
void append_decorations(std::vector<std::uint8_t>& p,
                        const std::vector<std::tuple<std::string, std::int16_t, std::int16_t,
                                                     std::int16_t, std::int16_t>>& decos) {
    for (const auto& [name, x, y, z, angle] : decos) {
        const std::size_t base = p.size();
        p.resize(base + kBlvDecorationSize, 0);
        for (std::size_t i = 0; i < name.size() && i < kBlvDecorationNameSize - 1; ++i) {
            p[base + i] = static_cast<std::uint8_t>(name[i]);
        }
        auto put_i16 = [&](std::size_t off, std::int16_t v) {
            const auto u = static_cast<std::uint16_t>(v);
            p[base + off] = static_cast<std::uint8_t>(u & 0xFF);
            p[base + off + 1] = static_cast<std::uint8_t>((u >> 8) & 0xFF);
        };
        put_i16(0x16, 1);
        put_i16(0x18, x);
        put_i16(0x1A, y);
        put_i16(0x1C, z);
        put_i16(0x1E, angle);
    }
}

const std::vector<std::array<std::int16_t, 3>> kSquare = {
    {0, 0, 0}, {256, 0, 0}, {256, 256, 0}, {0, 256, 0}, {128, 128, 0},
};

}  // namespace

TEST_CASE("a valid indoor map decodes header, vertices and faces", "[blv]") {
    const std::vector<FaceSpec> faces = {
        {{0, 1, 2, 3}, 0x100, "WallA"},
        {{0, 1, 4}, 0x201, ""},
    };
    auto entry = wrap(make_payload(kSquare, faces, "Dwarf Hold", "war1a"));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.header.kind == 1);
    REQUIRE(map.header.name == "Dwarf Hold");
    REQUIRE(map.header.name2 == "war1a");
    REQUIRE(map.header.unknown_6c == 111);
    REQUIRE(map.header.unknown_70 == 222);
    REQUIRE(map.header.unknown_74 == 333);

    REQUIRE(map.vertices.size() == 5);
    REQUIRE(map.vertices[1].x == 256);
    REQUIRE(map.vertices[2].y == 256);

    REQUIRE(map.faces.size() == 2);
    REQUIRE(map.faces[0].vertex_count == 4);
    REQUIRE(map.faces[0].vertex_ids == std::vector<std::uint16_t>{0, 1, 2, 3});
    REQUIRE(map.faces[0].texture_name == "WallA");
    REQUIRE(map.faces[0].attributes == 0x100);
    REQUIRE(map.faces[0].nz() == 1.0f);
    REQUIRE_FALSE(map.faces[0].invisible());

    // The second face is a triangle with no texture, flagged invisible.
    REQUIRE(map.faces[1].vertex_count == 3);
    REQUIRE(map.faces[1].texture_name.empty());
    REQUIRE(map.faces[1].invisible());

    REQUIRE(map.decoded_bytes == map.payload.size());
}

TEST_CASE("faces carry per-vertex texture coordinates", "[blv]") {
    // Arrays 4 and 5 of each face's six are the u and v coordinates; the
    // fixture fills array `a` with a*100 + k so the right ones are identifiable.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.faces[0].u == std::vector<std::int16_t>{400, 401, 402, 403});
    REQUIRE(map.faces[0].v == std::vector<std::int16_t>{500, 501, 502, 503});
    // One coordinate per vertex, not per stored array entry.
    REQUIRE(map.faces[0].u.size() == map.faces[0].vertex_ids.size());
}

TEST_CASE("faces of differing sizes keep the index block aligned", "[blv]") {
    // Each face owns six arrays of (n + 1) entries, so a wrong stride would
    // misalign every later face and its texture name.
    const std::vector<FaceSpec> faces = {
        {{0, 1, 2}, 0, "A"},
        {{0, 1, 2, 3}, 0, "B"},
        {{0, 1, 2, 3, 4}, 0, "C"},
        {{1, 2}, 0, "D"},
    };
    auto entry = wrap(make_payload(kSquare, faces));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.faces.size() == 4);
    REQUIRE(map.faces[0].texture_name == "A");
    REQUIRE(map.faces[1].texture_name == "B");
    REQUIRE(map.faces[2].texture_name == "C");
    REQUIRE(map.faces[3].texture_name == "D");
    REQUIRE(map.faces[2].vertex_ids == std::vector<std::uint16_t>{0, 1, 2, 3, 4});
    REQUIRE(map.faces[3].vertex_ids == std::vector<std::uint16_t>{1, 2});
}

TEST_CASE("a corrupt zlib checksum over intact data is accepted", "[blv]") {
    // Two shipped maps do exactly this; the original engine never verified the
    // Adler-32 trailer.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces), /*corrupt_checksum*/ true);

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.faces.size() == 1);
    REQUIRE(map.faces[0].texture_name == "WallA");
}

TEST_CASE("a corrupt checksum with a wrong declared size is rejected", "[blv]") {
    // The declared length is the only thing standing between "bad checksum,
    // good data" and "genuinely truncated", so it must be enforced.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    auto entry = wrap(payload, /*corrupt_checksum*/ true,
                      /*declared_override*/ static_cast<std::uint32_t>(payload.size() + 64));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::InflateFailed);
}

TEST_CASE("an entry too small for the wrapper is rejected", "[blv]") {
    const std::vector<std::byte> tiny(4, std::byte{0});
    BlvMap map;
    REQUIRE(parse_blv(tiny, map) == BlvError::TooSmall);
}

TEST_CASE("corrupt compressed data is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces));
    for (std::size_t i = kBlvWrapperSize + 4; i < entry.size(); ++i) {
        entry[i] = static_cast<std::byte>(0xAA);
    }
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::InflateFailed);
}

TEST_CASE("a payload shorter than the header is rejected", "[blv]") {
    const std::vector<std::uint8_t> stub(0x20, 0);
    auto entry = wrap(stub);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::HeaderTooSmall);
}

TEST_CASE("a vertex array running past the payload is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    put_u32(payload, kBlvVertexCountOffset, 100000);  // far more than are stored
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::Truncated);
}

TEST_CASE("a face referencing a missing vertex is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 99}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces));
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::BadGeometry);
}

TEST_CASE("an index block smaller than the faces need is rejected", "[blv]") {
    // The header's block size and the per-face vertex counts must agree; if
    // they do not, the texture names that follow would be misaligned.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    const std::uint32_t needed = static_cast<std::uint32_t>((4 + 1) * 2 * kBlvFaceArrayCount);
    put_u32(payload, 0x68, needed - 12);
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::BadGeometry);
}

TEST_CASE("an index block larger than the payload is rejected", "[blv]") {
    // Over-declaring pushes the texture names past the end, which the section
    // bounds catch before any geometry is read.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    const std::uint32_t needed = static_cast<std::uint32_t>((4 + 1) * 2 * kBlvFaceArrayCount);
    put_u32(payload, 0x68, needed + 4096);
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::Truncated);
}

TEST_CASE("a truncated texture-name block is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    payload.resize(payload.size() - 4);  // clip the last name
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::Truncated);
}

TEST_CASE("decorations are found in the undecoded tail", "[blv]") {
    // The sections between the texture names and the decorations are unknown,
    // so the array is located by scanning; give it some filler to skip past.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    payload.resize(payload.size() + 200, 0x11);  // undecoded filler
    append_decorations(payload, {
                                    {"Party Start", 100, 120, 0, 0},
                                    {"Torch01", 40, 60, 0, 64},
                                    {"Barrel", 200, 30, 0, 128},
                                    {"tree09", 10, 250, 0, 250},
                                });
    auto entry = wrap(payload);

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    const auto decos = find_decorations(map);
    REQUIRE(decos.size() == 4);
    REQUIRE(decos[0].name == "Party Start");
    REQUIRE(decos[0].x == 100);
    REQUIRE(decos[0].y == 120);
    REQUIRE(decos[1].name == "Torch01");
    REQUIRE(decos[1].angle == 64);
    REQUIRE(decos[3].name == "tree09");
}

TEST_CASE("a tail with no decorations yields none", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    payload.resize(payload.size() + 512, 0x7F);  // printable filler, no records
    auto entry = wrap(payload);

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(find_decorations(map).empty());
}

TEST_CASE("decorations far outside the level are not accepted", "[blv]") {
    // Coordinates outside the geometry's extents are what stops the scan
    // latching onto unrelated bytes.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces);
    append_decorations(payload, {
                                    {"Torch01", 30000, 30000, 30000, 0},
                                    {"Torch01", 30000, 30000, 30000, 0},
                                    {"Torch01", 30000, 30000, 30000, 0},
                                    {"Torch01", 30000, 30000, 30000, 0},
                                });
    auto entry = wrap(payload);

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(find_decorations(map).empty());
}

TEST_CASE("face extras are decoded after the texture names", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}, {{0, 1, 2}, 0, "WallB"}};
    auto entry = wrap(make_payload(kSquare, faces, "Test Level", "test", {{1, 10}, {0, 20}}));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.face_extras.size() == 2);
    REQUIRE(map.face_extras[0].face_index == 1);
    REQUIRE(map.face_extras[0].texture_origin_u == 10);
    REQUIRE(map.face_extras[0].texture_origin_v == 11);
    REQUIRE(map.face_extras[0].unknown_1a == 12);
    REQUIRE(map.face_extras[1].face_index == 0);
    REQUIRE(map.decoded_bytes == map.payload.size());
}

TEST_CASE("a face extra naming a missing face is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces, "Test Level", "test", {{99, 0}}));
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::BadGeometry);
}

TEST_CASE("a face extra without its 0xffff marker is rejected", "[blv]") {
    // The marker holds on all 35,485 records in the shipped maps, so a record
    // lacking it means the array is being read at the wrong offset.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces, "Test Level", "test", {{0, 5}});
    // Clear the marker of the first extra. The extras are followed by their
    // own name array, so count back past that too.
    const std::size_t marker = payload.size() - kBlvFaceExtraNameSize - kBlvFaceExtraSize + 0x0E;
    payload[marker] = 0;
    payload[marker + 1] = 0;
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::BadGeometry);
}

TEST_CASE("face extras carry a parallel name array", "[blv]") {
    // The names sit after the whole extra array, not interleaved with it, and
    // are empty far more often than not.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}, {{0, 1, 2}, 0, "WallB"}};
    auto entry = wrap(make_payload(kSquare, faces, "Test Level", "test", {{1, 10}, {0, 20}}));
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.face_extras.size() == 2);
    REQUIRE(map.face_extras[0].name == "extraA");
    REQUIRE(map.face_extras[1].name.empty());
    REQUIRE(map.decoded_bytes == map.payload.size());
}

TEST_CASE("a truncated face-extra name array is rejected", "[blv]") {
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto payload = make_payload(kSquare, faces, "Test Level", "test", {{0, 5}});
    payload.resize(payload.size() - 4);  // clip the last name
    auto entry = wrap(payload);
    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::Truncated);
}

TEST_CASE("attribute bit 0x80000000 says a face has an extra", "[blv]") {
    // The bit and the record agree in both directions across every shipped
    // map, apart from each array's face-zero sentinel; see docs/formats/blv.md.
    REQUIRE(kBlvFaceHasExtra == 0x80000000u);

    BlvFace flagged;
    flagged.attributes = kBlvFaceHasExtra;
    REQUIRE(flagged.has_extra());
    REQUIRE_FALSE(flagged.invisible());

    BlvFace plain;
    plain.attributes = 0x1100u;
    REQUIRE_FALSE(plain.has_extra());

    // The two bits are independent: an invisible face may still be described.
    BlvFace both;
    both.attributes = kBlvFaceHasExtra | 1u;
    REQUIRE(both.has_extra());
    REQUIRE(both.invisible());
}

TEST_CASE("a texture origin is signed", "[blv]") {
    // The origin is the negation of the face's lowest texture coordinate, so
    // it is negative wherever that coordinate is positive — which is most
    // faces. Reading it unsigned turns -1 into 65535.
    const std::vector<FaceSpec> faces = {{{0, 1, 2, 3}, 0, "WallA"}};
    auto entry = wrap(make_payload(kSquare, faces, "Test Level", "test", {{0, 0xFF9C}}));

    BlvMap map;
    REQUIRE(parse_blv(entry, map) == BlvError::None);
    REQUIRE(map.face_extras.size() == 1);
    REQUIRE(map.face_extras[0].texture_origin_u == -100);
}
