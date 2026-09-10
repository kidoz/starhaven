#ifndef STARHAVEN_GAME_SCRIPT_OBJECTS_HPP
#define STARHAVEN_GAME_SCRIPT_OBJECTS_HPP

#include <optional>
#include <span>

#include "core/data/item_stats.hpp"
#include "core/world/map_script.hpp"
#include "core/world/object_table.hpp"

namespace starhaven::game {

struct ObjectSpawnResources {
    std::uint16_t descriptor_index = 0;
    int item_id = 0;
};

// Resolve the two distinct ID joins used by opcode 34. A missing/unrepresentable
// descriptor is reported instead of silently selecting the unused slot zero.
// This prepares audit metadata only; it does not create a live sprite object.
[[nodiscard]] std::optional<ObjectSpawnResources>
resolve_object_spawn(const world::ObjectSpawnRequest& request,
                     std::span<const world::ObjectDescriptor> objects,
                     std::span<const data::ItemStatsEntry> items);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_OBJECTS_HPP
