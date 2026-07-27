#ifndef STARHAVEN_CORE_WORLD_MONSTER_SPAWN_HPP
#define STARHAVEN_CORE_WORLD_MONSTER_SPAWN_HPP

// Turning an outdoor map's spawn points into monsters.
//
// An outdoor map ships no placed actors: it ships places where monsters
// appear, and a design table row saying which. See
// docs/formats/odm-tile-index.md for the spawn points and
// docs/formats/text-tables.md for the encounter slots.

#include <cstdint>
#include <string_view>
#include <utility>

#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/world/odm_map.hpp"

namespace starhaven::world {

// How many monsters an encounter slot calls for, from a cell written "2-4".
struct SpawnCount {
    int low = 0;
    int high = 0;

    [[nodiscard]] bool empty() const noexcept { return low <= 0 || high < low; }
};

// Parse an encounter slot's count cell. "2-4" gives 2..4 and a bare "3" gives
// 3..3; anything else gives an empty range rather than a guess.
[[nodiscard]] SpawnCount parse_spawn_count(std::string_view text) noexcept;

// Which of the map's three encounter slots a spawn point's index selects.
// Returns 0, 1 or 2, or -1 when the index is zero.
//
// 808 of the 848 shipped spawn points carry 1, 2 or 3 and every map's set of
// indices is exactly the slots it fills. The other 40, on four maps, carry 6,
// 9, 10, 11 or 12, and wrapping them onto the three slots is what lands each
// of those on a slot its map actually fills; that reading is `inferred`.
[[nodiscard]] int encounter_slot_of(std::uint32_t spawn_index) noexcept;

// The 1-based MONSTERS.TXT row an encounter slot names, or 0.
//
// The slot gives a picture naming a triple — "Rat" is RatA, RatB and RatC —
// and means the first of the three. All 138 filled slots resolve; the one
// unique among them, the Demon Queen, has no triple and is prefixed with a
// "z" in the monster table instead.
[[nodiscard]] int encounter_monster_id(const data::MonsterStatsTable& monsters,
                                       const data::MapEncounter& slot) noexcept;

// Where one monster of a group stands: spread around the spawn point rather
// than stacked on it. `n` is its position in the group.
[[nodiscard]] std::pair<float, float> spawn_offset(int n, int group_size) noexcept;

// How far from its spawn point a group is spread, in world units.
constexpr float kSpawnGroupSpread = 300.0f;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MONSTER_SPAWN_HPP
