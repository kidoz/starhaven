#ifndef STARHAVEN_CORE_WORLD_CHEST_TABLE_HPP
#define STARHAVEN_CORE_WORLD_CHEST_TABLE_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// One row of the chest-appearance table (`DCHEST.BIN`). See docs/formats/dchest.md.
//
// The table is tiny — eight records — and pairs a chest's display name with
// the sprite-object it draws as. The object id at +0x22 is a row of
// DOBJLIST.BIN, and the leading constant at +0x20 is the same DSFT frame
// index on every row. `observed` for the layout on all eight records.
struct ChestAppearance {
    // Display name, e.g. "wooden chest", "sack", "metal chest".
    std::string name;

    // Constant 0x0a0e on all eight shipped records. It reads as a DSFT frame
    // index, and the table never varies it, but what selecting it would do is
    // not established; the engine reaches the art through `object_id`. `observed`
    // for the value, `unknown` for a use.
    std::uint16_t frame_index = 0;

    // The DOBJLIST object id this chest draws as. The eight values are 1..8,
    // every one a row of DOBJLIST.BIN. `observed`.
    std::uint16_t object_id = 0;
};

enum class ChestTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The eight chest appearances. A chest's placed instance carries the index into
// this table; the row then says which object to draw.
class ChestTable {
public:
    [[nodiscard]] static ChestTableError parse(std::span<const std::byte> entry, ChestTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<ChestAppearance>& entries() const noexcept { return entries_; }
    [[nodiscard]] const ChestAppearance* at(std::size_t index) const noexcept;

private:
    std::vector<ChestAppearance> entries_;
};

// Layout constants (see docs/formats/dchest.md).
constexpr std::size_t kChestRecordSize = 36;
constexpr std::size_t kChestNameSize = 32;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_CHEST_TABLE_HPP
