#include "core/world/blv_map.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <utility>

namespace starhaven::world {

using image::detail::inflate_all;
using image::detail::inflate_raw;

namespace {

// Header field offsets within the decompressed payload (docs/formats/blv.md).
constexpr std::uint32_t kKindOff = 0x00;
constexpr std::uint32_t kNameOff = 0x04;
constexpr std::uint32_t kName2Off = 0x50;
constexpr std::uint32_t kIndexBlockBytesOff = 0x68;

// The two name fields run up to the field that follows them. Their exact
// declared widths are not established; reading to the next field's offset is
// safe because the remainder is NUL in every observed map.
constexpr std::size_t kNameWidth = kName2Off - kNameOff;          // 76
constexpr std::size_t kName2Width = kIndexBlockBytesOff - kName2Off;  // 24

// Field offsets within an 80-byte face record.
constexpr std::uint32_t kFacePlaneOff = 0x00;        // 4 x i32, 16.16
constexpr std::uint32_t kFaceAttributesOff = 0x1C;   // u32
constexpr std::uint32_t kFaceVertexCountOff = 0x4D;  // u8

}  // namespace

BlvError parse_blv(std::span<const std::byte> entry, BlvMap& out) {
    out = BlvMap{};

    if (entry.size() < kBlvWrapperSize) {
        return BlvError::TooSmall;
    }

    // Same 8-byte wrapper the outdoor maps use: stream size, decompressed size,
    // then a zlib stream.
    io::ByteReader w{entry};
    if (!w.seek(0x04)) {
        return BlvError::TooSmall;
    }
    const std::uint32_t declared = w.read_u32_le();
    const std::span<const std::byte> stream = entry.subspan(kBlvWrapperSize);
    if (!inflate_all(stream, out.payload)) {
        // Two of the 52 shipped indoor maps carry a corrupt Adler-32 trailer
        // over intact deflate data, and the original engine never checked it.
        // Retry as raw deflate, but only accept the result if it is exactly as
        // long as the wrapper declares — otherwise a genuinely truncated
        // stream would slip through as a short map.
        if (declared == 0 || !inflate_raw(stream, out.payload) ||
            out.payload.size() != declared) {
            return BlvError::InflateFailed;
        }
    }
    if (declared != 0 && out.payload.size() != declared) {
        return BlvError::SizeMismatch;
    }
    if (out.payload.size() < kBlvHeaderSize) {
        return BlvError::HeaderTooSmall;
    }

    const std::span<const std::byte> p{
        reinterpret_cast<const std::byte*>(out.payload.data()), out.payload.size()};
    io::ByteReader r{p};

    // --- header ---
    if (!r.seek(kKindOff)) {
        return BlvError::HeaderTooSmall;
    }
    out.header.kind = r.read_u32_le();
    if (!r.seek(kNameOff) || !r.read_fixed_string(kNameWidth, out.header.name)) {
        return BlvError::HeaderTooSmall;
    }
    if (!r.seek(kName2Off) || !r.read_fixed_string(kName2Width, out.header.name2)) {
        return BlvError::HeaderTooSmall;
    }
    if (!r.seek(kIndexBlockBytesOff)) {
        return BlvError::HeaderTooSmall;
    }
    out.header.index_block_bytes = r.read_u32_le();
    out.header.unknown_6c = r.read_u32_le();
    out.header.unknown_70 = r.read_u32_le();
    out.header.unknown_74 = r.read_u32_le();

    // --- vertices ---
    if (!r.seek(kBlvVertexCountOffset)) {
        return BlvError::HeaderTooSmall;
    }
    const std::uint32_t vertex_count = r.read_u32_le();
    if (!r.ok()) {
        return BlvError::HeaderTooSmall;
    }

    const std::uint64_t vertices_start = kBlvVertexCountOffset + 4;
    const std::uint64_t vertices_end =
        vertices_start + static_cast<std::uint64_t>(vertex_count) * kBlvVertexSize;
    if (vertices_end + 4 > p.size()) {
        return BlvError::Truncated;
    }

    out.vertices.reserve(vertex_count);
    for (std::uint32_t i = 0; i < vertex_count; ++i) {
        if (!r.seek(static_cast<std::size_t>(
                vertices_start + static_cast<std::uint64_t>(i) * kBlvVertexSize))) {
            return BlvError::Truncated;
        }
        BlvVertex v;
        v.x = static_cast<std::int16_t>(r.read_u16_le());
        v.y = static_cast<std::int16_t>(r.read_u16_le());
        v.z = static_cast<std::int16_t>(r.read_u16_le());
        out.vertices.push_back(v);
    }

    // --- faces ---
    if (!r.seek(static_cast<std::size_t>(vertices_end))) {
        return BlvError::Truncated;
    }
    const std::uint32_t face_count = r.read_u32_le();
    if (!r.ok()) {
        return BlvError::Truncated;
    }

    const std::uint64_t faces_start = vertices_end + 4;
    const std::uint64_t faces_end =
        faces_start + static_cast<std::uint64_t>(face_count) * kBlvFaceSize;
    const std::uint64_t index_end =
        faces_end + out.header.index_block_bytes;
    const std::uint64_t names_end =
        index_end + static_cast<std::uint64_t>(face_count) * kBlvTextureNameSize;
    if (names_end > p.size()) {
        return BlvError::Truncated;
    }

    out.faces.reserve(face_count);

    // The per-face index arrays are packed one face after another: each face
    // owns six u16 arrays of `vertex_count + 1` entries, and only the first
    // (the vertex ids) is decoded here. Walking them is what makes the block
    // size in the header a real check rather than a stored constant.
    std::uint64_t index_cursor = faces_end;

    for (std::uint32_t i = 0; i < face_count; ++i) {
        const std::uint64_t base =
            faces_start + static_cast<std::uint64_t>(i) * kBlvFaceSize;
        BlvFace f;

        if (!r.seek(static_cast<std::size_t>(base + kFacePlaneOff))) {
            return BlvError::Truncated;
        }
        f.normal_x = r.read_i32_le();
        f.normal_y = r.read_i32_le();
        f.normal_z = r.read_i32_le();
        f.plane_distance = r.read_i32_le();

        if (!r.seek(static_cast<std::size_t>(base + kFaceAttributesOff))) {
            return BlvError::Truncated;
        }
        f.attributes = r.read_u32_le();

        if (!r.seek(static_cast<std::size_t>(base + kFaceVertexCountOff))) {
            return BlvError::Truncated;
        }
        f.vertex_count = r.read_u8();

        // Each array stores one extra entry, a copy of the first id closing the
        // ring, so the group is (n + 1) entries wide.
        const std::uint64_t entries = static_cast<std::uint64_t>(f.vertex_count) + 1;
        const std::uint64_t group_bytes = entries * 2 * kBlvFaceArrayCount;
        if (index_cursor + group_bytes > index_end) {
            return BlvError::BadGeometry;
        }

        if (!r.seek(static_cast<std::size_t>(index_cursor))) {
            return BlvError::Truncated;
        }
        f.vertex_ids.reserve(f.vertex_count);
        for (std::uint8_t k = 0; k < f.vertex_count; ++k) {
            const std::uint16_t id = r.read_u16_le();
            if (id >= vertex_count) {
                return BlvError::BadGeometry;
            }
            f.vertex_ids.push_back(id);
        }
        index_cursor += group_bytes;

        if (!r.seek(static_cast<std::size_t>(
                index_end + static_cast<std::uint64_t>(i) * kBlvTextureNameSize))) {
            return BlvError::Truncated;
        }
        if (!r.read_fixed_string(kBlvTextureNameSize, f.texture_name)) {
            return BlvError::Truncated;
        }

        out.faces.push_back(std::move(f));
    }

    // The index arrays must account for exactly the block the header declares.
    // A mismatch means the per-face vertex counts and the block disagree, so
    // the texture names that follow would be misaligned too.
    if (index_cursor != index_end) {
        return BlvError::BadGeometry;
    }
    if (!r.ok()) {
        return BlvError::Truncated;
    }
    out.decoded_bytes = names_end;
    return BlvError::None;
}

}  // namespace starhaven::world
