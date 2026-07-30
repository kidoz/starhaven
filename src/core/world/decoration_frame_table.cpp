#include "core/world/decoration_frame_table.hpp"

#include <utility>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

DecorationFrameTableError DecorationFrameTable::parse(std::span<const std::byte> entry,
                                                     DecorationFrameTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return DecorationFrameTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return DecorationFrameTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return DecorationFrameTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok() ||
        static_cast<std::uint64_t>(count) * kDecorationFrameRecordSize + 4U != raw.size()) {
        return DecorationFrameTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kDecorationFrameRecordSize;
        DecorationFrame frame;
        if (!r.seek(base) || !r.read_fixed_string(kDecorationNameSize, frame.group_name) ||
            !r.read_fixed_string(kDecorationNameSize, frame.sprite_name)) {
            return DecorationFrameTableError::BadCount;
        }
        frame.flags = r.read_u16_le();
        frame.frame_count = r.read_u16_le();
        frame.duration = r.read_u16_le();
        if (!r.skip(2) || !r.ok()) {  // trailing u16, zero on every shipped frame
            return DecorationFrameTableError::BadCount;
        }
        out.entries_.push_back(std::move(frame));
    }
    return DecorationFrameTableError::None;
}

}  // namespace starhaven::world
