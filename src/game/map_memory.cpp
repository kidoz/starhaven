#include "game/map_memory.hpp"

#include <algorithm>

#include "game/combat.hpp"

namespace starhaven::game {

MapMemoryResult restore_map_memory(const MapMemory& memory, MapMemoryUse use,
                                   std::int64_t arrival_day, world::MapSession& session,
                                   Battle& battle, std::set<int>& opened_chests,
                                   std::span<world::MonsterAnimation> shown_kind) {
    if (use == MapMemoryUse::Revisit && session.refill_days > 0 &&
        arrival_day >= memory.remembered_day &&
        static_cast<std::uint64_t>(arrival_day) -
                static_cast<std::uint64_t>(memory.remembered_day) >=
            static_cast<std::uint64_t>(session.refill_days)) {
        return MapMemoryResult::Expired;
    }

    opened_chests = memory.opened_chests;
    for (auto& door : session.doors) {
        door.open = std::ranges::find(memory.open_doors, door.id) != memory.open_doors.end();
        door.progress = door.open ? 1.0f : 0.0f;
        world::stand_door(session, door);
    }
    if (!session.doors.empty()) {
        world::rebuild_indoor_collision(session);
    }
    for (const std::size_t actor : memory.dead) {
        battle.kill(actor);
        if (actor < shown_kind.size()) {
            shown_kind[actor] = world::MonsterAnimation::Death;
        }
    }
    return MapMemoryResult::Restored;
}

}  // namespace starhaven::game
