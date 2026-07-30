#include "core/world/chest_table.hpp"

#include <utility>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

ChestTableError ChestTable::parse(std::span<const std::byte> entry, ChestTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return ChestTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return ChestTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return ChestTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok() ||
        static_cast<std::uint64_t>(count) * kChestRecordSize + 4U != raw.size()) {
        return ChestTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kChestRecordSize;
        ChestAppearance appearance;
        if (!r.seek(base) || !r.read_fixed_string(kChestNameSize, appearance.name)) {
            return ChestTableError::BadCount;
        }
        appearance.frame_index = r.read_u16_le();
        appearance.object_id = r.read_u16_le();
        if (!r.ok()) {
            return ChestTableError::BadCount;
        }
        out.entries_.push_back(std::move(appearance));
    }
    return ChestTableError::None;
}

const ChestAppearance* ChestTable::at(std::size_t index) const noexcept {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

}  // namespace starhaven::world
