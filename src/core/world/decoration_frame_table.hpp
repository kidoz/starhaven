#ifndef STARHAVEN_CORE_WORLD_DECORATION_FRAME_TABLE_HPP
#define STARHAVEN_CORE_WORLD_DECORATION_FRAME_TABLE_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// One row of the decoration frame table (`DIFT.BIN`). See docs/formats/dift.md.
//
// Sixty-one thirty-two-byte records in the DSFT shape: a group name set on a
// group's first frame, a sprite name, flags, a frame count, and a duration.
// The table animates the torch and brazier glows the decorations wear.
// `observed` for the layout on all sixty-one records; the sprite-name join is
// not resolved here and is tagged in the documentation.
struct DecorationFrame {
    // Set on a group's first frame only; empty on the frames that follow.
    std::string group_name;
    // A sprite reference for the frame.
    std::string sprite_name;
    std::uint16_t flags = 0;       // 0 or 1 on every shipped frame
    std::uint16_t frame_count = 0; // on a group's first frame, equals the group's length
    std::uint16_t duration = 0;    // per-frame, in the frame tables' shared unit
};

enum class DecorationFrameTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The decoration frame table. Records are returned in file order; a group's
// records are contiguous and its first frame carries the group name and frame
// count.
class DecorationFrameTable {
public:
    [[nodiscard]] static DecorationFrameTableError parse(std::span<const std::byte> entry,
                                                        DecorationFrameTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<DecorationFrame>& entries() const noexcept { return entries_; }

private:
    std::vector<DecorationFrame> entries_;
};

// Layout constants (see docs/formats/dift.md).
constexpr std::size_t kDecorationFrameRecordSize = 32;
constexpr std::size_t kDecorationNameSize = 12;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_DECORATION_FRAME_TABLE_HPP
