#ifndef STARHAVEN_CORE_WORLD_DECORATION_TABLE_HPP
#define STARHAVEN_CORE_WORLD_DECORATION_TABLE_HPP

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::world {

// One row of the global decoration table (`DDECLIST.BIN`).
// See docs/formats/odm-decorations.md.
struct DecorationTableEntry {
    std::string name;   // matches a map's decoration name, e.g. "CampfireOn"
    std::string group;  // e.g. "tree", "cactus", "test"

    // How much room the decoration takes, in world units: 96 for trees, 52
    // for cacti. No two decorations on a map are ever placed closer than the
    // sum of their values — 0 of 1,737 near pairs on Sweet Water, against
    // violations when a single constant is used for every kind — which is what
    // a collision radius does to a level's layout. `inferred`
    std::uint16_t radius = 0;      // +0x42
    std::uint16_t unknown_44 = 0;  // 76 for trees
    std::uint16_t sprite_id = 0;   // consecutive across sibling entries

    // The ambient sound this decoration makes, resolved through `DSOUNDS.BIN`.
    // Zero on all but seven of the 230 shipped rows: most decorations are
    // silent.
    std::uint16_t sound_id = 0;
};

enum class DecorationTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The global decoration table. A map's decoration `kind` indexes it, and its
// `name` is what the map's parallel name array repeats.
class DecorationTable {
public:
    DecorationTable() = default;

    // `entry` is the raw stored bytes of the archive's DDECLIST.BIN entry.
    [[nodiscard]] static DecorationTableError parse(std::span<const std::byte> entry,
                                                    DecorationTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<DecorationTableEntry>& entries() const noexcept {
        return entries_;
    }

    // Resolve a decoration `kind`. Returns nullptr when the table is shorter.
    [[nodiscard]] const DecorationTableEntry* at(std::size_t kind) const noexcept;

    // Resolve by name, ignoring case. Indoor maps name their decorations
    // without carrying a type id, so this is the only way in for them.
    [[nodiscard]] const DecorationTableEntry* find(std::string_view name) const noexcept;

private:
    std::vector<DecorationTableEntry> entries_;
    std::map<std::string, std::size_t, std::less<>> by_name_;
};

// Layout constants (see docs/formats/odm-decorations.md).
constexpr std::size_t kDecorationTableRecordSize = 80;
constexpr std::size_t kDecorationTableNameSize = 32;
constexpr std::size_t kDecorationTableSoundOffset = 0x4C;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_DECORATION_TABLE_HPP
