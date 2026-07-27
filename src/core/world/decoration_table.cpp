#include "core/world/decoration_table.hpp"

#include <cctype>
#include <cstddef>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;
constexpr std::size_t kGroupOffset = 0x20;
constexpr std::size_t kGroupSize = 32;
constexpr std::size_t kUnknown42Offset = 0x42;
constexpr std::size_t kSpriteIdOffset = 0x48;

std::string lowercase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

DecorationTableError DecorationTable::parse(std::span<const std::byte> entry,
                                            DecorationTable& out) {
    out.entries_.clear();
    out.by_name_.clear();

    if (entry.size() <= kEntryHeaderSize) {
        return DecorationTableError::TooSmall;
    }
    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return DecorationTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return DecorationTableError::BadCount;
    }

    const auto bytes = std::as_bytes(std::span<const std::uint8_t>(raw));
    io::ByteReader r(bytes);
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok()) {
        return DecorationTableError::BadCount;
    }
    if (static_cast<std::size_t>(count) * kDecorationTableRecordSize + 4U != raw.size()) {
        return DecorationTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4U + static_cast<std::size_t>(i) * kDecorationTableRecordSize;
        DecorationTableEntry e;

        // The record is read field by field rather than sequentially: most of
        // its 80 bytes are still unidentified, and naming the gaps would claim
        // more than is known.
        io::ByteReader f(bytes);
        if (!f.seek(base) || !f.read_fixed_string(kDecorationTableNameSize, e.name)) {
            return DecorationTableError::BadCount;
        }
        if (!f.seek(base + kGroupOffset) || !f.read_fixed_string(kGroupSize, e.group)) {
            return DecorationTableError::BadCount;
        }
        if (!f.seek(base + kUnknown42Offset)) {
            return DecorationTableError::BadCount;
        }
        e.radius = f.read_u16_le();
        e.unknown_44 = f.read_u16_le();
        if (!f.seek(base + kSpriteIdOffset)) {
            return DecorationTableError::BadCount;
        }
        e.sprite_id = f.read_u16_le();
        if (!f.seek(base + kDecorationTableSoundOffset)) {
            return DecorationTableError::BadCount;
        }
        e.sound_id = f.read_u16_le();
        if (!f.ok()) {
            return DecorationTableError::BadCount;
        }

        if (!e.name.empty()) {
            out.by_name_.emplace(lowercase(e.name), i);
        }
        out.entries_.push_back(std::move(e));
    }
    return DecorationTableError::None;
}

const DecorationTableEntry* DecorationTable::at(std::size_t kind) const noexcept {
    return kind < entries_.size() ? &entries_[kind] : nullptr;
}

const DecorationTableEntry* DecorationTable::find(std::string_view name) const noexcept {
    const auto it = by_name_.find(lowercase(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

}  // namespace starhaven::world
