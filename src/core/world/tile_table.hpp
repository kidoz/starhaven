#ifndef OPENMM6_CORE_WORLD_TILE_TABLE_HPP
#define OPENMM6_CORE_WORLD_TILE_TABLE_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openmm6::world {

// One row of the global ground-tile table (`DTILE.BIN`). See
// docs/formats/dtile.md.
struct TileTableEntry {
    std::string name;              // BITMAPS.LOD entry name; may be empty
    std::uint16_t unknown_a = 0;   // zero in every observed record
    std::uint16_t unknown_b = 0;   // zero in every observed record
    std::uint16_t group = 0;       // tileset group id, matches the ODM header
    std::uint16_t section = 0;     // ordinal within the group; not a pixel index
    std::uint16_t attributes = 0;  // bit flags; 0, 2, 64, 512 observed
};

enum class TileTableError {
    None,
    TooSmall,        // fewer bytes than the 48-byte entry header needs
    NotCompressed,   // the inner zlib stream is missing or malformed
    BadCount,        // record count disagrees with the decompressed length
};

// The global tile table, indexed directly by an ODM tilemap byte.
//
// Parses the raw `DTILE.BIN` entry exactly as stored in `icons.lod`: a
// 48-byte header followed by a zlib stream, which inflates to a u32 record
// count and that many 26-byte records.
class TileTable {
public:
    TileTable() = default;

    // `entry` is the raw stored bytes of the archive's DTILE.BIN entry.
    [[nodiscard]] static TileTableError parse(std::span<const std::byte> entry,
                                              TileTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<TileTableEntry>& entries() const noexcept {
        return entries_;
    }

    // Resolve a tilemap byte. The lookup is a direct index (docs/formats/
    // dtile.md); returns nullptr when the table is shorter than the index,
    // which malformed or truncated data can produce.
    [[nodiscard]] const TileTableEntry* at(std::uint8_t tile_index) const noexcept;

private:
    std::vector<TileTableEntry> entries_;
};

}  // namespace openmm6::world

#endif  // OPENMM6_CORE_WORLD_TILE_TABLE_HPP
