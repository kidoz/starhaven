#include "core/world/map_event.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <utility>

namespace starhaven::world {

using image::detail::inflate_all;

MapEventError parse_map_event(std::span<const std::byte> entry,
                              MapEventFile& out) {
    out = MapEventFile{};

    if (entry.size() < kEventWrapperSize) {
        return MapEventError::TooSmall;
    }

    io::ByteReader r{entry};
    r.seek(0x00);
    [[maybe_unused]] const std::uint32_t stream_size = r.read_u32_le();
    r.seek(0x04);
    const std::uint32_t decompressed_size = r.read_u32_le();
    (void)stream_size;

    const std::span<const std::byte> zlib_block = entry.subspan(kEventWrapperSize);
    if (!inflate_all(zlib_block, out.payload)) {
        return MapEventError::InflateFailed;
    }

    // The inflated length must match the declared decompressed size, unless the
    // declared size is zero (treat as unknown).
    if (decompressed_size != 0 && out.payload.size() != decompressed_size) {
        return MapEventError::SizeMismatch;
    }
    return MapEventError::None;
}

std::vector<EventTableRecord>
enumerate_event_table(const MapEventFile& file, std::size_t max_records) {
    std::vector<EventTableRecord> out;

    const auto& p = file.payload;
    // Walk fixed-stride records from the table start until we run out of
    // payload, hit the caller's cap, or pass the plausible table region. A
    // record is "populated" if type != 0 or the name field has any nonzero byte.
    for (std::size_t i = 0; i < max_records; ++i) {
        const std::size_t rec = kEventTableOffset + i * kEventRecordSize;
        if (rec + kEventRecordNameOffset + kEventRecordNameMax > p.size()) {
            break;  // truncated: stop safely
        }
        const std::int32_t type =
            static_cast<std::int32_t>(p[rec]) |
            (static_cast<std::int32_t>(p[rec + 1]) << 8) |
            (static_cast<std::int32_t>(p[rec + 2]) << 16) |
            (static_cast<std::int32_t>(p[rec + 3]) << 24);

        const std::size_t name_off = rec + kEventRecordNameOffset;
        const bool name_nonzero =
            std::any_of(p.begin() + name_off,
                        p.begin() + name_off + kEventRecordNameMax,
                        [](std::uint8_t b) { return b != 0; });

        if (type == 0 && !name_nonzero) {
            continue;  // empty slot
        }

        EventTableRecord r;
        r.type = type;
        // Copy up to the first NUL or the max prefix.
        const std::size_t end = std::min(p.size(), name_off + kEventRecordNameMax);
        std::size_t n = name_off;
        while (n < end && p[n] != 0) {
            r.name.push_back(static_cast<char>(p[n]));
            ++n;
        }
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace starhaven::world
