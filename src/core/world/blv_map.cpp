#include "core/world/blv_map.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <algorithm>
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
constexpr std::size_t kNameWidth = kName2Off - kNameOff;              // 76
constexpr std::size_t kName2Width = kIndexBlockBytesOff - kName2Off;  // 24

// Which of a face's six index arrays hold what. Array 0 is the vertex ids and
// arrays 4 and 5 are texture coordinates; 1..3 carry small signed values
// (-3..3) that are not needed for rendering.
constexpr std::size_t kArrayVertexIds = 0;
constexpr std::size_t kArrayU = 4;
constexpr std::size_t kArrayV = 5;

// Field offsets within an 80-byte face record.
constexpr std::uint32_t kFacePlaneOff = 0x00;  // 4 x i32, 16.16
constexpr std::uint32_t kFaceArrayPointersOff = 0x20;
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
        if (declared == 0 || !inflate_raw(stream, out.payload) || out.payload.size() != declared) {
            return BlvError::InflateFailed;
        }
    }
    if (declared != 0 && out.payload.size() != declared) {
        return BlvError::SizeMismatch;
    }
    if (out.payload.size() < kBlvHeaderSize) {
        return BlvError::HeaderTooSmall;
    }

    const std::span<const std::byte> p{reinterpret_cast<const std::byte*>(out.payload.data()),
                                       out.payload.size()};
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
    out.header.event_state_bytes = r.read_u32_le();

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
        if (!r.seek(static_cast<std::size_t>(vertices_start +
                                             static_cast<std::uint64_t>(i) * kBlvVertexSize))) {
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
    const std::uint64_t index_end = faces_end + out.header.index_block_bytes;
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
        const std::uint64_t base = faces_start + static_cast<std::uint64_t>(i) * kBlvFaceSize;
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

        // The face's six arrays sit side by side, each `entries` wide.
        const auto array_at = [&](std::size_t which) { return index_cursor + which * entries * 2; };

        // The first array pointer is stale, but its value minus the array's
        // own offset is the address the payload was loaded at. Every face must
        // agree, which is both a check on the walk and a way to read the
        // file's other stale pointers.
        if (r.seek(static_cast<std::size_t>(base + kFaceArrayPointersOff))) {
            const std::uint32_t stored = r.read_u32_le();
            const auto implied = static_cast<std::uint32_t>(stored - index_cursor);
            if (i == 0) {
                out.pointer_base = implied;
            } else if (out.pointer_base != implied) {
                out.pointer_base = 0;
            }
        }

        if (!r.seek(static_cast<std::size_t>(array_at(kArrayVertexIds)))) {
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

        if (!r.seek(static_cast<std::size_t>(array_at(kArrayU)))) {
            return BlvError::Truncated;
        }
        f.u.reserve(f.vertex_count);
        for (std::uint8_t k = 0; k < f.vertex_count; ++k) {
            f.u.push_back(static_cast<std::int16_t>(r.read_u16_le()));
        }
        if (!r.seek(static_cast<std::size_t>(array_at(kArrayV)))) {
            return BlvError::Truncated;
        }
        f.v.reserve(f.vertex_count);
        for (std::uint8_t k = 0; k < f.vertex_count; ++k) {
            f.v.push_back(static_cast<std::int16_t>(r.read_u16_le()));
        }

        index_cursor += group_bytes;

        if (!r.seek(static_cast<std::size_t>(index_end + static_cast<std::uint64_t>(i) *
                                                             kBlvTextureNameSize))) {
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
    // --- face extras ---
    //
    // A count-prefixed array of 36-byte records, each naming a face. Two
    // invariants hold on all 52 shipped maps and are enforced here: the face
    // index is always in range, and the u16 at +0x0e is always 0xffff. The
    // second is what makes a misaligned read impossible to miss.
    if (!r.seek(static_cast<std::size_t>(names_end))) {
        return BlvError::Truncated;
    }
    const std::uint32_t extra_count = r.read_u32_le();
    if (!r.ok()) {
        return BlvError::Truncated;
    }
    const std::uint64_t extras_start = names_end + 4;
    const std::uint64_t extras_end =
        extras_start + static_cast<std::uint64_t>(extra_count) * kBlvFaceExtraSize;
    // Each extra is followed by a 10-byte name, the same arrangement faces use.
    const std::uint64_t extra_names_end =
        extras_end + static_cast<std::uint64_t>(extra_count) * kBlvFaceExtraNameSize;
    if (extra_names_end > p.size()) {
        return BlvError::Truncated;
    }

    out.face_extras_offset = extras_start;
    out.face_extras.reserve(extra_count);
    for (std::uint32_t i = 0; i < extra_count; ++i) {
        const std::uint64_t base = extras_start + static_cast<std::uint64_t>(i) * kBlvFaceExtraSize;
        if (!r.seek(static_cast<std::size_t>(base + 0x0C))) {
            return BlvError::Truncated;
        }
        BlvFaceExtra e;
        e.face_index = r.read_u16_le();
        const std::uint16_t marker = r.read_u16_le();
        if (e.face_index >= face_count || marker != 0xFFFFu) {
            return BlvError::BadGeometry;
        }
        if (!r.seek(static_cast<std::size_t>(base + 0x14))) {
            return BlvError::Truncated;
        }
        e.texture_origin_u = static_cast<std::int16_t>(r.read_u16_le());
        e.texture_origin_v = static_cast<std::int16_t>(r.read_u16_le());
        if (!r.seek(static_cast<std::size_t>(base + 0x1A))) {
            return BlvError::Truncated;
        }
        e.event_id = r.read_u16_le();

        if (!r.seek(static_cast<std::size_t>(extras_end + static_cast<std::uint64_t>(i) *
                                                              kBlvFaceExtraNameSize))) {
            return BlvError::Truncated;
        }
        if (!r.read_fixed_string(kBlvFaceExtraNameSize, e.name)) {
            return BlvError::Truncated;
        }
        out.face_extras.push_back(std::move(e));
    }
    if (!r.ok()) {
        return BlvError::Truncated;
    }

    out.decoded_bytes = extra_names_end;
    return BlvError::None;
}

namespace {

// Field offsets within a 32-byte decoration record.
constexpr std::size_t kDecoFlagsOff = 0x16;
constexpr std::size_t kDecoPosOff = 0x18;
constexpr std::size_t kDecoAngleOff = 0x1E;

// A run this long of records that all validate is treated as the array. One or
// two matches could be coincidence in a large payload; four in a row, each with
// a printable name and in-bounds coordinates, is not.
constexpr std::size_t kMinRun = 4;

// Coordinates may sit slightly outside the geometry's own extents (a torch
// mounted in a wall recess, say), so allow a margin before rejecting.
constexpr int kExtentMargin = 2000;

struct Extent {
    int lo[3] = {0, 0, 0};
    int hi[3] = {0, 0, 0};
};

[[nodiscard]] Extent vertex_extent(const BlvMap& map) {
    Extent e;
    if (map.vertices.empty()) {
        return e;
    }
    e.lo[0] = e.hi[0] = map.vertices[0].x;
    e.lo[1] = e.hi[1] = map.vertices[0].y;
    e.lo[2] = e.hi[2] = map.vertices[0].z;
    for (const auto& v : map.vertices) {
        const int c[3] = {v.x, v.y, v.z};
        for (int i = 0; i < 3; ++i) {
            e.lo[i] = std::min(e.lo[i], c[i]);
            e.hi[i] = std::max(e.hi[i], c[i]);
        }
    }
    return e;
}

[[nodiscard]] bool read_decoration(const std::vector<std::uint8_t>& p, std::size_t off,
                                   const Extent& extent, BlvDecoration& out) {
    if (off + kBlvDecorationSize > p.size()) {
        return false;
    }

    // The name must be non-empty, printable, and terminated inside its field.
    std::string name;
    for (std::size_t i = 0; i < kBlvDecorationNameSize; ++i) {
        const std::uint8_t c = p[off + i];
        if (c == 0)
            break;
        if (c < 32 || c >= 127)
            return false;
        name.push_back(static_cast<char>(c));
    }
    if (name.empty() || name.size() == kBlvDecorationNameSize) {
        return false;
    }

    const auto u16_at = [&](std::size_t o) {
        return static_cast<std::uint16_t>(p[off + o] | (p[off + o + 1] << 8));
    };
    const auto i16_at = [&](std::size_t o) { return static_cast<std::int16_t>(u16_at(o)); };

    const std::uint16_t flags = u16_at(kDecoFlagsOff);
    if (flags > 1) {
        return false;
    }
    const std::int16_t c[3] = {i16_at(kDecoPosOff), i16_at(kDecoPosOff + 2),
                               i16_at(kDecoPosOff + 4)};
    for (int i = 0; i < 3; ++i) {
        if (c[i] < extent.lo[i] - kExtentMargin || c[i] > extent.hi[i] + kExtentMargin) {
            return false;
        }
    }

    out.name = std::move(name);
    out.flags = flags;
    out.x = c[0];
    out.y = c[1];
    out.z = c[2];  // the scan's fallback reads 16-bit coordinates
    out.angle = i16_at(kDecoAngleOff);
    return true;
}

}  // namespace

BlvDecorationBlock find_decoration_block(const BlvMap& map) {
    BlvDecorationBlock out;
    if (map.vertices.empty() || map.decoded_bytes >= map.payload.size()) {
        return out;
    }
    const Extent extent = vertex_extent(map);
    // A decoration can stand a little outside the geometry's own bounds.
    constexpr std::int32_t kMargin = 2048;
    const auto printable = [](const std::vector<std::uint8_t>& p, std::size_t at,
                              std::size_t size) {
        std::size_t length = 0;
        while (length < size && p[at + length] != 0) {
            const std::uint8_t c = p[at + length];
            if (c < 32 || c >= 127) {
                return false;
            }
            ++length;
        }
        return length > 0 && length < size;
    };

    const auto& p = map.payload;
    const auto i32_at = [&p](std::size_t at) {
        std::int32_t v = 0;
        std::memcpy(&v, p.data() + at, sizeof(v));
        return v;
    };
    const auto u32_at = [&p](std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy(&v, p.data() + at, sizeof(v));
        return v;
    };

    constexpr std::uint32_t kMaxDecorations = 8192;
    for (std::size_t at = static_cast<std::size_t>(map.decoded_bytes); at + 4 <= p.size();
         at += 2) {
        const std::uint32_t count = u32_at(at);
        if (count == 0 || count > kMaxDecorations) {
            continue;
        }
        const std::size_t records = at + 4;
        const std::size_t names =
            records + static_cast<std::size_t>(count) * kBlvDecorationRecordSize;
        const std::size_t end = names + static_cast<std::size_t>(count) * kBlvDecorationSize;
        if (end > p.size()) {
            continue;
        }

        // Every placement has to sit near the level, and every name has to be
        // a name. Both together are a strong enough filter that no shipped map
        // has a second offset passing it.
        bool plausible = true;
        for (std::uint32_t i = 0; i < count && plausible; ++i) {
            const std::size_t o = records + static_cast<std::size_t>(i) * kBlvDecorationRecordSize;
            const std::int32_t c[3] = {i32_at(o + 4), i32_at(o + 8), i32_at(o + 12)};
            for (int k = 0; k < 3 && plausible; ++k) {
                plausible = c[k] >= extent.lo[k] - kMargin && c[k] <= extent.hi[k] + kMargin;
            }
        }
        for (std::uint32_t i = 0; i < count && plausible; ++i) {
            const std::size_t o = names + static_cast<std::size_t>(i) * kBlvDecorationSize;
            plausible = printable(p, o, kBlvDecorationNameSize);
        }
        // Keep looking: a very small count passes the filter by accident on
        // three of the 52 maps, always before the real block. The real one
        // holds more, so the largest candidate is the one meant.
        if (plausible && count > out.count) {
            out.offset = at;
            out.count = count;
        }
    }
    return out;
}

std::size_t find_decorations_offset(const BlvMap& map) {
    if (map.vertices.empty() || map.decoded_bytes >= map.payload.size()) {
        return map.payload.size();
    }
    const Extent extent = vertex_extent(map);
    std::size_t anchor = map.payload.size();
    for (std::size_t off = static_cast<std::size_t>(map.decoded_bytes);
         off + kMinRun * kBlvDecorationSize <= map.payload.size(); off += 2) {
        bool run = true;
        for (std::size_t k = 0; k < kMinRun; ++k) {
            BlvDecoration probe;
            if (!read_decoration(map.payload, off + k * kBlvDecorationSize, extent, probe)) {
                run = false;
                break;
            }
        }
        if (run) {
            anchor = off;
            break;
        }
    }
    if (anchor == map.payload.size()) {
        return anchor;
    }
    std::size_t start = anchor;
    while (start >= kBlvDecorationSize && start - kBlvDecorationSize >= map.decoded_bytes) {
        BlvDecoration probe;
        if (!read_decoration(map.payload, start - kBlvDecorationSize, extent, probe)) {
            break;
        }
        start -= kBlvDecorationSize;
    }
    return start;
}

std::vector<BlvDecoration> find_decorations(const BlvMap& map) {
    std::vector<BlvDecoration> out;

    // The exact path: the block says how many there are, where each stands in
    // full 32-bit coordinates, and what each is called.
    if (const BlvDecorationBlock block = find_decoration_block(map); block.found()) {
        const auto& p = map.payload;
        out.reserve(block.count);
        for (std::uint32_t i = 0; i < block.count; ++i) {
            const std::size_t r =
                block.records() + static_cast<std::size_t>(i) * kBlvDecorationRecordSize;
            const std::size_t n = block.names() + static_cast<std::size_t>(i) * kBlvDecorationSize;
            BlvDecoration d;
            for (std::size_t k = 0; k < kBlvDecorationNameSize && p[n + k] != 0; ++k) {
                d.name.push_back(static_cast<char>(p[n + k]));
            }
            std::memcpy(&d.x, p.data() + r + 4, sizeof(d.x));
            std::memcpy(&d.y, p.data() + r + 8, sizeof(d.y));
            std::memcpy(&d.z, p.data() + r + 12, sizeof(d.z));
            std::memcpy(&d.flags, p.data() + n + kDecoFlagsOff, sizeof(d.flags));
            std::memcpy(&d.angle, p.data() + n + kDecoAngleOff, sizeof(d.angle));
            out.push_back(std::move(d));
        }
        return out;
    }

    if (map.vertices.empty() || map.decoded_bytes >= map.payload.size()) {
        return out;
    }
    const Extent extent = vertex_extent(map);

    // Find the first offset that begins a run of valid records.
    std::size_t anchor = map.payload.size();
    for (std::size_t off = static_cast<std::size_t>(map.decoded_bytes);
         off + kMinRun * kBlvDecorationSize <= map.payload.size(); off += 2) {
        bool run = true;
        for (std::size_t k = 0; k < kMinRun; ++k) {
            BlvDecoration probe;
            if (!read_decoration(map.payload, off + k * kBlvDecorationSize, extent, probe)) {
                run = false;
                break;
            }
        }
        if (run) {
            anchor = off;
            break;
        }
    }
    if (anchor == map.payload.size()) {
        return out;
    }

    // Extend backwards to the true first record, then read forwards.
    std::size_t start = anchor;
    while (start >= kBlvDecorationSize && start - kBlvDecorationSize >= map.decoded_bytes) {
        BlvDecoration probe;
        if (!read_decoration(map.payload, start - kBlvDecorationSize, extent, probe)) {
            break;
        }
        start -= kBlvDecorationSize;
    }

    for (std::size_t off = start; off + kBlvDecorationSize <= map.payload.size();
         off += kBlvDecorationSize) {
        BlvDecoration d;
        if (!read_decoration(map.payload, off, extent, d)) {
            break;
        }
        out.push_back(std::move(d));
    }
    return out;
}


std::vector<BlvLight> extract_lights(const BlvMap& map) {
    std::vector<BlvLight> out;
    const BlvDecorationBlock block = find_decoration_block(map);
    if (!block.found()) {
        return out;
    }
    const std::size_t at = block.end();
    const auto& payload = map.payload;
    if (at + 4 > payload.size()) {
        return out;
    }
    std::uint32_t count = 0;
    std::memcpy(&count, payload.data() + at, sizeof(count));
    constexpr std::size_t kLightRecordSize = 12;
    if (count == 0 || count > 10000 ||
        at + 4 + static_cast<std::size_t>(count) * kLightRecordSize > payload.size()) {
        return out;
    }
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t r = at + 4 + static_cast<std::size_t>(i) * kLightRecordSize;
        BlvLight light;
        std::memcpy(&light.x, payload.data() + r, sizeof(light.x));
        std::memcpy(&light.y, payload.data() + r + 2, sizeof(light.y));
        std::memcpy(&light.z, payload.data() + r + 4, sizeof(light.z));
        std::memcpy(&light.brightness, payload.data() + r + 8, sizeof(light.brightness));
        std::memcpy(&light.radius, payload.data() + r + 10, sizeof(light.radius));
        out.push_back(light);
    }
    return out;
}

}  // namespace starhaven::world
