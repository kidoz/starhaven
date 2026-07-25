#include "core/world/map_event.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <utility>

namespace starhaven::world {

using image::detail::inflate_all;

MapEventError parse_map_event(std::span<const std::byte> entry, MapEventFile& out) {
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

std::vector<EventTableRecord> enumerate_event_table(const MapEventFile& file,
                                                    std::size_t max_records) {
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
        const std::int32_t type = static_cast<std::int32_t>(p[rec]) |
                                  (static_cast<std::int32_t>(p[rec + 1]) << 8) |
                                  (static_cast<std::int32_t>(p[rec + 2]) << 16) |
                                  (static_cast<std::int32_t>(p[rec + 3]) << 24);

        const std::size_t name_off = rec + kEventRecordNameOffset;
        const bool name_nonzero =
            std::any_of(p.begin() + name_off, p.begin() + name_off + kEventRecordNameMax,
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

// --- Actors ----------------------------------------------------------------

std::vector<MapActor> extract_actors(const MapEventFile& file, std::size_t max_records) {
    std::vector<MapActor> out;
    const auto& p = file.payload;

    for (std::size_t i = 0; i < max_records; ++i) {
        const std::uint64_t base = static_cast<std::uint64_t>(kEventTableOffset) +
                                   static_cast<std::uint64_t>(i) * kEventRecordSize;
        if (base + kEventRecordSize > p.size()) {
            break;
        }

        // The name runs to its first NUL. A slot whose name is short or
        // non-printable is past the end of the array: several shipped files
        // have one such slot, holding an implausible position of (0, 0, 31744).
        std::string name;
        const std::size_t name_at = static_cast<std::size_t>(base) + kEventRecordNameOffset;
        for (std::size_t k = 0; k < 32; ++k) {
            const std::uint8_t ch = p[name_at + k];
            if (ch == 0)
                break;
            if (ch < 32 || ch >= 127) {
                name.clear();
                break;
            }
            name.push_back(static_cast<char>(ch));
        }
        if (name.size() < kActorMinNameLength) {
            break;
        }

        MapActor a;
        a.name = std::move(name);
        a.monster_id = p[static_cast<std::size_t>(base) + kActorMonsterIdOffset];
        a.variant = p[static_cast<std::size_t>(base) + kActorVariantOffset];
        const std::size_t pos = static_cast<std::size_t>(base) + kActorPositionOffset;
        const auto i16_at = [&](std::size_t o) {
            return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[o]) |
                                             (static_cast<std::uint16_t>(p[o + 1]) << 8));
        };
        a.x = i16_at(pos);
        a.y = i16_at(pos + 2);
        a.z = i16_at(pos + 4);
        out.push_back(std::move(a));
    }
    return out;
}

}  // namespace starhaven::world
