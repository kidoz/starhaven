#ifndef STARHAVEN_CORE_WORLD_SPRITE_FRAME_TABLE_HPP
#define STARHAVEN_CORE_WORLD_SPRITE_FRAME_TABLE_HPP

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::world {

// How many view directions a directional sprite has: `base` plus a digit 0..4.
constexpr int kSpriteViewCount = 5;

// One row of the global sprite frame table (`DSFT.BIN`).
// See docs/formats/dsft.md.
struct SpriteFrame {
    // Non-empty only on a group's first frame; it names the whole animation,
    // e.g. "arc1wka". Later frames of the group leave it blank.
    std::string group_name;

    // A SPRITES.LOD entry name — or, when `view_directional()`, the base that a
    // view digit completes: "arc1wkA" plus 0..4.
    std::string sprite_name;

    // Size multiplier in 16.16 fixed point. 1.0 is the common value; trees run
    // to 2.1 and pedestals down to 0.7.
    std::int32_t scale = 0;

    std::uint32_t flags = 0;

    // Close to the sprite's own palette id but not reliably equal to it; the
    // sprite header is authoritative. See docs/formats/dsft.md.
    std::uint16_t palette_id = 0;

    // How long this frame shows, in the table's own time units.
    std::uint16_t duration = 0;

    // The group's total animation length. Set on the first frame only, and
    // equal to the sum of the group's durations.
    std::uint16_t group_length = 0;

    // Another frame of this group follows. Clear on a group's last frame.
    [[nodiscard]] bool has_next() const noexcept { return (flags & 0x1u) != 0; }

    // This frame begins a group. Equivalent to a non-empty `group_name`; the
    // file states both, which is what makes it a usable cross-check.
    [[nodiscard]] bool starts_group() const noexcept { return (flags & 0x4u) != 0; }

    // The sprite exists in five view directions, and `sprite_name` is only the
    // base: a view digit completes it.
    [[nodiscard]] bool view_directional() const noexcept { return (flags & 0xE000u) == 0xE000u; }

    [[nodiscard]] float scale_factor() const noexcept {
        return static_cast<float>(scale) / 65536.0f;
    }
};

enum class SpriteFrameError {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the two counts do not account for the inflated block
    BadLookup,      // the lookup array points outside the frame array
};

// The global sprite frame table: every animation in the game, as an ordered
// list of frames grouped by name.
//
// This is what turns a name in `DMONLIST.BIN` or `DDECLIST.BIN` — which name
// animations, not sprites — into the SPRITES.LOD entries to draw and the order
// to draw them in.
class SpriteFrameTable {
public:
    SpriteFrameTable() = default;

    // `entry` is the raw stored bytes of the archive's DSFT.BIN entry.
    [[nodiscard]] static SpriteFrameError parse(std::span<const std::byte> entry,
                                                SpriteFrameTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return frames_.size(); }
    [[nodiscard]] const std::vector<SpriteFrame>& frames() const noexcept { return frames_; }

    // The file's own alphabetical index over the groups: each entry is the
    // frame index of one group's first frame. Not needed for lookup — that
    // uses a map built while parsing — but it verifies the segmentation.
    [[nodiscard]] const std::vector<std::uint16_t>& lookup() const noexcept { return lookup_; }

    // How many groups the file declares. Five names occur twice, so `group()`
    // — which looks up by name — can reach five fewer than this.
    [[nodiscard]] std::size_t group_count() const noexcept { return lookup_.size(); }
    [[nodiscard]] std::size_t distinct_group_names() const noexcept { return groups_.size(); }

    // All frames of a named animation, in order. Empty when the name is not a
    // group. Matching ignores case, as the shipped names are inconsistent
    // ("ARC1STA" in one table, "arc1sta" in another).
    [[nodiscard]] std::span<const SpriteFrame> group(std::string_view name) const;

    // The frame a named animation shows at `time`, which wraps around the
    // group's length. Returns nullptr when the name is not a group.
    [[nodiscard]] const SpriteFrame* frame_at(std::string_view name, std::uint32_t time) const;

    // The SPRITES.LOD entry to draw for a frame seen from `view` (0..4).
    // Non-directional frames ignore `view`.
    [[nodiscard]] static std::string sprite_entry(const SpriteFrame& frame, int view);

private:
    std::vector<SpriteFrame> frames_;
    std::vector<std::uint16_t> lookup_;
    // Lowercased group name to the index of its first frame.
    std::map<std::string, std::uint32_t, std::less<>> groups_;
};

// Layout constants (see docs/formats/dsft.md).
constexpr std::size_t kSpriteFrameSize = 56;
constexpr std::size_t kSpriteFrameNameSize = 12;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_SPRITE_FRAME_TABLE_HPP
