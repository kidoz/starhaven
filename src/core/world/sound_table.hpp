#ifndef STARHAVEN_CORE_WORLD_SOUND_TABLE_HPP
#define STARHAVEN_CORE_WORLD_SOUND_TABLE_HPP

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::world {

// One row of the global sound table (`DSOUNDS.BIN`).
// See docs/formats/dsounds.md.
struct SoundTableEntry {
    // The `Sounds/Audio.snd` entry to play, e.g. "campfire".
    std::string name;

    // The id the rest of the game refers to this sound by.
    std::uint32_t id = 0;

    // A small group code. 0 covers the continuous and movement sounds, 1 the
    // interface and door sounds, 2 the bulk of one-shot effects, and 4 the
    // party's spoken lines. What the engine does with it is not established.
    std::uint32_t group = 0;
};

enum class SoundTableError : std::uint8_t {
    None,
    TooSmall,       // fewer bytes than the 48-byte entry header needs
    NotCompressed,  // the inner zlib stream is missing or malformed
    BadCount,       // the record count disagrees with the decompressed length
};

// The global sound table: what an id in the rest of the game data means.
//
// `DDECLIST.BIN` names an ambient sound this way, and monster sound sets live
// in a numbered block here (see docs/formats/dsounds.md).
class SoundTable {
public:
    SoundTable() = default;

    // `entry` is the raw stored bytes of the archive's DSOUNDS.BIN entry.
    [[nodiscard]] static SoundTableError parse(std::span<const std::byte> entry, SoundTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<SoundTableEntry>& entries() const noexcept { return entries_; }

    // Resolve a sound id. Three ids are used twice; the first wins. Returns
    // nullptr when no record carries the id.
    [[nodiscard]] const SoundTableEntry* find(std::uint32_t id) const noexcept;

    // Resolve by archive name, ignoring case. The shipped names are
    // inconsistently cased across tables.
    [[nodiscard]] const SoundTableEntry* find_by_name(std::string_view name) const noexcept;

private:
    std::vector<SoundTableEntry> entries_;
    std::map<std::uint32_t, std::size_t> by_id_;
    std::map<std::string, std::size_t, std::less<>> by_name_;
};

// The action a monster sound covers, as its offset within the monster's block
// of ten ids (see docs/formats/dsounds.md).
enum class MonsterSound : std::uint32_t {
    Attack = 0,
    Die = 1,
    Charge = 2,
    Fidget = 3,
};

// Monster sound sets start here and advance ten ids per monster that has one.
constexpr std::uint32_t kMonsterSoundBase = 1000;
constexpr std::uint32_t kMonsterSoundStride = 10;

// Layout constants (see docs/formats/dsounds.md).
constexpr std::size_t kSoundRecordSize = 112;
constexpr std::size_t kSoundNameSize = 32;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_SOUND_TABLE_HPP
