#include "core/world/portrait_frame_table.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

PortraitFrameTableError PortraitFrameTable::parse(std::span<const std::byte> entry,
                                                  PortraitFrameTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return PortraitFrameTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return PortraitFrameTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return PortraitFrameTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok() ||
        static_cast<std::uint64_t>(count) * kPortraitFrameRecordSize + 4U != raw.size()) {
        return PortraitFrameTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kPortraitFrameRecordSize;
        PortraitFrame frame;
        if (!r.seek(base)) {
            return PortraitFrameTableError::BadCount;
        }
        frame.id = r.read_u16_le();
        frame.cell_id = r.read_u16_le();
        frame.width = r.read_u16_le();
        frame.height = r.read_u16_le();
        frame.count = r.read_u16_le();
        if (!r.ok()) {
            return PortraitFrameTableError::BadCount;
        }
        out.entries_.push_back(frame);
    }
    return PortraitFrameTableError::None;
}

}  // namespace starhaven::world
