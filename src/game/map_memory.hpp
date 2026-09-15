#ifndef STARHAVEN_GAME_MAP_MEMORY_HPP
#define STARHAVEN_GAME_MAP_MEMORY_HPP

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "core/world/map_session.hpp"

namespace starhaven::game {

class Battle;

struct MapMemory {
    std::set<int> opened_chests;
    std::vector<std::uint32_t> open_doors;
    std::vector<std::size_t> dead;
    std::int64_t remembered_day = 0;
};

enum class MapMemoryUse : std::uint8_t { Revisit, SavedSnapshot };
enum class MapMemoryResult : std::uint8_t { Expired, Restored };

// A saved active map is a snapshot, even when the previous live session was
// much later. Only ordinary revisits apply the destination's refill policy.
[[nodiscard]] MapMemoryResult restore_map_memory(const MapMemory& memory, MapMemoryUse use,
                                                 std::int64_t arrival_day,
                                                 world::MapSession& session, Battle& battle,
                                                 std::set<int>& opened_chests,
                                                 std::span<world::MonsterAnimation> shown_kind);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_MAP_MEMORY_HPP
