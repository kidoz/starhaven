#ifndef STARHAVEN_CORE_WORLD_TEXTURE_FRAME_TABLE_HPP
#define STARHAVEN_CORE_WORLD_TEXTURE_FRAME_TABLE_HPP

// DTFT.BIN: the texture frame table, DSFT's small sibling for the wall
// textures that move — three moss-and-wood alternations and the two
// haunted-painting loops, nineteen records in all. Same container as
// DTILE.BIN, and per record the same shape DSFT uses: a name, a duration,
// a group total on the first frame, and flags where bit 1 starts a group
// and bit 0 says another frame follows. `observed` for the shape on all
// nineteen records; see docs/formats/dtft.md.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

struct TextureFrame {
    std::string name;   // a BITMAPS.LOD entry
    int duration = 0;   // in the frame tables' shared time unit
    bool starts = false;
    bool more = false;
};

struct TextureAnimation {
    std::vector<TextureFrame> frames;
    int total = 0;  // sum of durations

    // The frame shown at `ticks`, wrapping. Empty groups answer nothing.
    [[nodiscard]] const std::string* frame_at(std::uint32_t ticks) const {
        if (frames.empty() || total <= 0) {
            return nullptr;
        }
        int at = static_cast<int>(ticks % static_cast<std::uint32_t>(total));
        for (const auto& frame : frames) {
            at -= frame.duration;
            if (at < 0) {
                return &frame.name;
            }
        }
        return &frames.back().name;
    }
};

// Parse the raw stored entry. Returns the animations in file order; an
// entry that does not inflate or divide into 20-byte records parses empty.
[[nodiscard]] inline std::vector<TextureAnimation> parse_texture_frames(
    std::span<const std::byte> entry) {
    std::vector<TextureAnimation> out;
    if (entry.size() < 48) {
        return out;
    }
    std::vector<std::uint8_t> plain;
    if (!image::detail::inflate_all(entry.subspan(48), plain) || plain.size() < 4) {
        return out;
    }
    const std::size_t count = static_cast<std::size_t>(plain[0]) | (plain[1] << 8) |
                              (plain[2] << 16) | (static_cast<std::size_t>(plain[3]) << 24);
    constexpr std::size_t kStride = 20;
    if (count == 0 || 4 + count * kStride != plain.size()) {
        return out;
    }
    TextureAnimation current;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 4 + i * kStride;
        TextureFrame frame;
        for (std::size_t k = 0; k < 12 && plain[at + k] != 0; ++k) {
            frame.name += static_cast<char>(plain[at + k]);
        }
        frame.duration = plain[at + 14] | (plain[at + 15] << 8);
        const int flags = plain[at + 18] | (plain[at + 19] << 8);
        frame.starts = (flags & 2) != 0;
        frame.more = (flags & 1) != 0;
        if (frame.starts && !current.frames.empty()) {
            out.push_back(std::move(current));
            current = {};
        }
        current.total += frame.duration;
        const bool last = !frame.more;
        current.frames.push_back(std::move(frame));
        if (last) {
            out.push_back(std::move(current));
            current = {};
        }
    }
    if (!current.frames.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_TEXTURE_FRAME_TABLE_HPP
