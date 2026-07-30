#ifndef STARHAVEN_CORE_WORLD_PORTRAIT_FRAME_TABLE_HPP
#define STARHAVEN_CORE_WORLD_PORTRAIT_FRAME_TABLE_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace starhaven::world {

// One row of the portrait frame table (`DPFT.BIN`). See docs/formats/dpft.md.
//
// Sixty-seven ten-byte rows of five little-endian u16. The leading id runs
// 0..57 and a second cell id tracks it closely; the middle pair read as a width
// and height (1, 2, 4, 8, 16, with height also taking 0 and 26); the last is a
// small count (0, 1, 4 or 5). `observed` for the layout on all sixty-seven
// records; the widths/heights-as-pixels reading is `inferred`.
struct PortraitFrame {
    std::uint16_t id = 0;       // 0..57 across the table
    std::uint16_t cell_id = 0;  // tracks id; 1..53
    std::uint16_t width = 0;    // 1, 2, 4, 8 or 16
    std::uint16_t height = 0;   // 0, 1, 2, 4, 8, 16 or 26
    std::uint16_t count = 0;    // 0, 1, 4 or 5
};

enum class PortraitFrameTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The portrait frame table.
class PortraitFrameTable {
public:
    [[nodiscard]] static PortraitFrameTableError parse(std::span<const std::byte> entry,
                                                       PortraitFrameTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<PortraitFrame>& entries() const noexcept { return entries_; }

private:
    std::vector<PortraitFrame> entries_;
};

// Layout constants (see docs/formats/dpft.md).
constexpr std::size_t kPortraitFrameRecordSize = 10;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_PORTRAIT_FRAME_TABLE_HPP
