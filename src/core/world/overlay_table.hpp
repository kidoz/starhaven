#ifndef STARHAVEN_CORE_WORLD_OVERLAY_TABLE_HPP
#define STARHAVEN_CORE_WORLD_OVERLAY_TABLE_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace starhaven::world {

// One row of the overlay table (`DOVERLAY.BIN`). See docs/formats/doverlay.md.
//
// The table maps an overlay id to a scale, with ninety-six rows. The ids are
// distinct and cluster by thousands; the scale reads as an 8.8 fixed-point
// multiplier near 2.0. The trailing u16 is zero on every shipped row.
// `observed` for the layout on all ninety-six records; the meaning the engine
// gives the id is `inferred`, and the 8.8 reading of the scale is `inferred`.
struct OverlayEntry {
    // Distinct across the table, 10..11220; cluster in thousands. Likely the
    // overlay's own object/sprite id. `observed` for the values.
    std::uint32_t id = 0;

    // Raw u16. Read as 8.8 fixed point it sits near 2.0 (512..536 are common),
    // which is consistent with a size multiplier for an overlay sprite.
    // `observed` for the stored words; `inferred` for the fixed-point reading.
    std::uint16_t scale = 0;
};

enum class OverlayTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The overlay table.
class OverlayTable {
public:
    [[nodiscard]] static OverlayTableError parse(std::span<const std::byte> entry, OverlayTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<OverlayEntry>& entries() const noexcept { return entries_; }

private:
    std::vector<OverlayEntry> entries_;
};

// Layout constants (see docs/formats/doverlay.md).
constexpr std::size_t kOverlayRecordSize = 8;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_OVERLAY_TABLE_HPP
