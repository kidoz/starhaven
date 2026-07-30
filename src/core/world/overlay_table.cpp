#include "core/world/overlay_table.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

OverlayTableError OverlayTable::parse(std::span<const std::byte> entry, OverlayTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return OverlayTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return OverlayTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return OverlayTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok() ||
        static_cast<std::uint64_t>(count) * kOverlayRecordSize + 4U != raw.size()) {
        return OverlayTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kOverlayRecordSize;
        OverlayEntry entry_row;
        if (!r.seek(base)) {
            return OverlayTableError::BadCount;
        }
        entry_row.id = r.read_u32_le();
        entry_row.scale = r.read_u16_le();
        if (!r.skip(2) || !r.ok()) {  // trailing u16, zero on every shipped row
            return OverlayTableError::BadCount;
        }
        out.entries_.push_back(entry_row);
    }
    return OverlayTableError::None;
}

}  // namespace starhaven::world
