#include "core/world/monster_list.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <cstdint>
#include <utility>

namespace starhaven::world {

namespace {

// The stored entry is a 48-byte header followed by a zlib stream, the same
// shape DTILE.BIN and the image entries use.
constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

MonsterListError MonsterList::parse(std::span<const std::byte> entry, MonsterList& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return MonsterListError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return MonsterListError::NotCompressed;
    }
    if (raw.size() < 4) {
        return MonsterListError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok()) {
        return MonsterListError::BadCount;
    }
    // The count and the record size must account for the whole block exactly;
    // a mismatch means the record size is wrong and every field would be
    // read at the wrong offset.
    if (static_cast<std::uint64_t>(count) * kMonsterRecordSize + 4 != raw.size()) {
        return MonsterListError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kMonsterRecordSize;
        MonsterListEntry e;
        if (!r.seek(base + kMonsterHeightOffset)) {
            return MonsterListError::BadCount;
        }
        e.height = r.read_u16_le();
        e.radius = r.read_u16_le();
        if (!r.seek(base + kMonsterSoundOffset)) {
            return MonsterListError::BadCount;
        }
        for (auto& sound : e.sounds) {
            sound = r.read_u16_le();
        }
        if (!r.seek(base + kMonsterNameOffset) || !r.read_fixed_string(kMonsterNameSize, e.name)) {
            return MonsterListError::BadCount;
        }
        for (std::size_t a = 0; a < kMonsterAnimationCount; ++a) {
            if (!r.seek(base + kMonsterAnimationOffset + a * kMonsterAnimationNameSize) ||
                !r.read_fixed_string(kMonsterAnimationNameSize, e.animations[a])) {
                return MonsterListError::BadCount;
            }
        }
        out.entries_.push_back(std::move(e));
    }
    return MonsterListError::None;
}

const MonsterListEntry* MonsterList::at(std::size_t id) const noexcept {
    return id < entries_.size() ? &entries_[id] : nullptr;
}

}  // namespace starhaven::world
