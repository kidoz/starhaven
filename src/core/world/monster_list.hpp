#ifndef STARHAVEN_CORE_WORLD_MONSTER_LIST_HPP
#define STARHAVEN_CORE_WORLD_MONSTER_LIST_HPP

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// The animation slots each monster declares, in table order.
enum class MonsterAnimation : std::uint8_t {
    Stand = 0,
    Walk,
    Attack1,
    Attack2,
    Wince,
    Death,
    Defend,
    Fidget,
    Count,
};

constexpr std::size_t kMonsterAnimationCount =
    static_cast<std::size_t>(MonsterAnimation::Count);

// One row of the global monster table (`DMONLIST.BIN`).
// See docs/formats/dmonlist.md.
struct MonsterListEntry {
    std::string name;   // e.g. "ArcherA", "PeasantF1B"

    // Sprite base names, one per animation. A base name is completed with a
    // view-direction digit: "PFEMSTA" plus 0..4 selects the SPRITES.LOD entry.
    std::array<std::string, kMonsterAnimationCount> animations;

    [[nodiscard]] const std::string& animation(MonsterAnimation a) const noexcept {
        return animations[static_cast<std::size_t>(a)];
    }
};

enum class MonsterListError {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The global monster table, indexed by an actor record's monster id.
class MonsterList {
public:
    MonsterList() = default;

    // `entry` is the raw stored bytes of the archive's DMONLIST.BIN entry.
    [[nodiscard]] static MonsterListError parse(std::span<const std::byte> entry,
                                                MonsterList& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<MonsterListEntry>& entries() const noexcept {
        return entries_;
    }

    // Resolve a monster id; nullptr when the table is shorter than the id,
    // which malformed or truncated data can produce.
    [[nodiscard]] const MonsterListEntry* at(std::size_t id) const noexcept;

private:
    std::vector<MonsterListEntry> entries_;
};

// Layout constants (see docs/formats/dmonlist.md).
constexpr std::size_t kMonsterRecordSize = 148;
constexpr std::size_t kMonsterNameOffset = 0x10;
constexpr std::size_t kMonsterNameSize = 32;
constexpr std::size_t kMonsterAnimationOffset = 0x30;
constexpr std::size_t kMonsterAnimationNameSize = 10;

// Sprite view directions: a base name plus 0..4.
constexpr std::size_t kMonsterViewCount = 5;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MONSTER_LIST_HPP
