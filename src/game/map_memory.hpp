#ifndef STARHAVEN_GAME_MAP_MEMORY_HPP
#define STARHAVEN_GAME_MAP_MEMORY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/world/map_session.hpp"
#include "game/save.hpp"

namespace starhaven::game {

class Battle;

struct MapMemory {
    std::set<int> opened_chests;
    std::vector<std::uint32_t> open_doors;
    std::vector<std::size_t> dead;
    std::int64_t remembered_day = 0;
};

using MapMemories = std::map<std::string, MapMemory>;

// Archive map names are ASCII and case-insensitive; keep the extension so
// indoor and outdoor resources cannot share state accidentally.
[[nodiscard]] std::string map_memory_key(std::string_view file);
[[nodiscard]] MapMemory capture_map_memory(const world::MapSession& session, const Battle& battle,
                                           const std::set<int>& opened_chests, std::int64_t day);
// Legacy duplicate spellings resolve in stored order: the last snapshot wins.
[[nodiscard]] MapMemories load_map_memories(std::span<const SaveState::RememberedMap> records);
[[nodiscard]] std::vector<SaveState::RememberedMap> save_map_memories(const MapMemories& memories,
                                                                      std::string_view active_file,
                                                                      const MapMemory& active);

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
