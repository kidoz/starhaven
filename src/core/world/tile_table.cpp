#include "core/world/tile_table.hpp"

#include <cstdint>
#include <vector>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

// The stored entry is a 48-byte header followed by a zlib stream, the same
// shape the image entries use. See docs/formats/dtile.md.
constexpr std::size_t kEntryHeaderSize = 48;
constexpr std::size_t kRecordSize = 26;
constexpr std::size_t kNameSize = 16;

}  // namespace

TileTableError TileTable::parse(std::span<const std::byte> entry, TileTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return TileTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return TileTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return TileTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok()) {
        return TileTableError::BadCount;
    }

    // The count must account for the payload exactly. A table claiming more
    // records than the buffer holds is rejected rather than truncated, so a
    // corrupt entry cannot yield a partly-populated table that silently
    // renders the wrong ground.
    if (static_cast<std::size_t>(count) * kRecordSize + 4U != raw.size()) {
        return TileTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TileTableEntry e;
        if (!r.read_fixed_string(kNameSize, e.name)) {
            return TileTableError::BadCount;
        }
        e.unknown_a = r.read_u16_le();
        e.unknown_b = r.read_u16_le();
        e.group = r.read_u16_le();
        e.section = r.read_u16_le();
        e.attributes = r.read_u16_le();
        if (!r.ok()) {
            return TileTableError::BadCount;
        }
        out.entries_.push_back(std::move(e));
    }
    return TileTableError::None;
}

const TileTableEntry* TileTable::at(std::uint8_t tile_index) const noexcept {
    const std::size_t i = tile_index;
    if (i >= entries_.size()) {
        return nullptr;
    }
    return &entries_[i];
}

}  // namespace starhaven::world
