#include "core/world/sprite_frame_table.hpp"

#include <cctype>
#include <cstddef>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

// The stored entry is a 48-byte header followed by a zlib stream, the same
// shape the image entries use. See docs/formats/dsft.md.
constexpr std::size_t kEntryHeaderSize = 48;

// A block at +0x18 the engine fills in at load time; zero in every shipped
// record.
constexpr std::size_t kRuntimeBlockSize = 16;

std::string lowercase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

SpriteFrameError SpriteFrameTable::parse(std::span<const std::byte> entry, SpriteFrameTable& out) {
    out.frames_.clear();
    out.lookup_.clear();
    out.groups_.clear();

    if (entry.size() <= kEntryHeaderSize) {
        return SpriteFrameError::TooSmall;
    }
    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return SpriteFrameError::NotCompressed;
    }
    if (raw.size() < 8) {
        return SpriteFrameError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t frame_count = r.read_u32_le();
    const std::uint32_t group_count = r.read_u32_le();
    if (!r.ok()) {
        return SpriteFrameError::BadCount;
    }

    // Both counts must account for the block exactly: frames of 56 bytes, then
    // one u16 of lookup per group. A table that does not close is rejected
    // rather than truncated, so a corrupt entry cannot yield a partly
    // populated table that animates the wrong sprites.
    const std::size_t expected = 8U + static_cast<std::size_t>(frame_count) * kSpriteFrameSize +
                                 static_cast<std::size_t>(group_count) * 2U;
    if (expected != raw.size()) {
        return SpriteFrameError::BadCount;
    }

    out.frames_.reserve(frame_count);
    for (std::uint32_t i = 0; i < frame_count; ++i) {
        SpriteFrame f;
        if (!r.read_fixed_string(kSpriteFrameNameSize, f.group_name) ||
            !r.read_fixed_string(kSpriteFrameNameSize, f.sprite_name)) {
            return SpriteFrameError::BadCount;
        }
        // A block the engine fills in at load time; zero in every shipped
        // record, and skipped rather than exposed.
        if (!r.skip(kRuntimeBlockSize)) {
            return SpriteFrameError::BadCount;
        }
        f.scale = r.read_i32_le();
        f.flags = r.read_u32_le();
        f.palette_id = r.read_u16_le();
        if (!r.skip(2)) {  // always zero
            return SpriteFrameError::BadCount;
        }
        f.duration = r.read_u16_le();
        f.group_length = r.read_u16_le();
        if (!r.ok()) {
            return SpriteFrameError::BadCount;
        }
        if (!f.group_name.empty()) {
            out.groups_.emplace(lowercase(f.group_name), i);
        }
        out.frames_.push_back(std::move(f));
    }

    out.lookup_.reserve(group_count);
    for (std::uint32_t i = 0; i < group_count; ++i) {
        const std::uint16_t index = r.read_u16_le();
        if (!r.ok() || index >= frame_count) {
            return SpriteFrameError::BadLookup;
        }
        out.lookup_.push_back(index);
    }
    return SpriteFrameError::None;
}

std::span<const SpriteFrame> SpriteFrameTable::group(std::string_view name) const {
    const auto it = groups_.find(lowercase(name));
    if (it == groups_.end()) {
        return {};
    }
    const std::size_t first = it->second;
    std::size_t last = first + 1;
    // A group runs until the next frame that starts one. The name and the flag
    // agree in the shipped table; the name is used here because it is what
    // segmentation was verified against.
    while (last < frames_.size() && frames_[last].group_name.empty()) {
        ++last;
    }
    return std::span<const SpriteFrame>(frames_).subspan(first, last - first);
}

const SpriteFrame* SpriteFrameTable::frame_at(std::string_view name, std::uint32_t time) const {
    const std::span<const SpriteFrame> frames = group(name);
    if (frames.empty()) {
        return nullptr;
    }
    const std::uint32_t length = frames.front().group_length;
    // A still image — one frame, or a group whose durations are all zero.
    if (length == 0) {
        return &frames.front();
    }
    std::uint32_t at = time % length;
    for (const SpriteFrame& f : frames) {
        if (at < f.duration) {
            return &f;
        }
        at -= f.duration;
    }
    // Unreachable while the durations sum to the length, which the shipped
    // table satisfies for every group. Falling back to the last frame keeps a
    // hand-made table from returning nothing.
    return &frames.back();
}

std::string SpriteFrameTable::sprite_entry(const SpriteFrame& frame, int view) {
    if (!frame.view_directional()) {
        return frame.sprite_name;
    }
    const int clamped = view < 0 ? 0 : (view >= kSpriteViewCount ? kSpriteViewCount - 1 : view);
    return frame.sprite_name + static_cast<char>('0' + clamped);
}

}  // namespace starhaven::world
