#include "core/world/sound_table.hpp"

#include <cctype>
#include <cstddef>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

// A block the engine fills in at load time; zero in every shipped record.
constexpr std::size_t kRuntimeBlockSize = kSoundRecordSize - kSoundNameSize - 8;

std::string lowercase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

SoundTableError SoundTable::parse(std::span<const std::byte> entry, SoundTable& out) {
    out.entries_.clear();
    out.by_id_.clear();
    out.by_name_.clear();

    if (entry.size() <= kEntryHeaderSize) {
        return SoundTableError::TooSmall;
    }
    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return SoundTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return SoundTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok()) {
        return SoundTableError::BadCount;
    }
    // The count must account for the block exactly, as with the other tables
    // in this archive: a table that does not close is rejected rather than
    // truncated.
    if (static_cast<std::size_t>(count) * kSoundRecordSize + 4U != raw.size()) {
        return SoundTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        SoundTableEntry e;
        if (!r.read_fixed_string(kSoundNameSize, e.name)) {
            return SoundTableError::BadCount;
        }
        e.id = r.read_u32_le();
        e.group = r.read_u32_le();
        if (!r.skip(kRuntimeBlockSize) || !r.ok()) {
            return SoundTableError::BadCount;
        }
        // Three ids repeat, and one record has no name. First occurrence wins
        // in both indexes; the vector keeps every row.
        out.by_id_.emplace(e.id, i);
        if (!e.name.empty()) {
            out.by_name_.emplace(lowercase(e.name), i);
        }
        out.entries_.push_back(std::move(e));
    }
    return SoundTableError::None;
}

const SoundTableEntry* SoundTable::find(std::uint32_t id) const noexcept {
    const auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &entries_[it->second];
}

const SoundTableEntry* SoundTable::find_by_name(std::string_view name) const noexcept {
    const auto it = by_name_.find(lowercase(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

}  // namespace starhaven::world
