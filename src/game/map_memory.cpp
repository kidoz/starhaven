#include "game/map_memory.hpp"

#include <algorithm>

#include "game/combat.hpp"

namespace starhaven::game {

std::string map_memory_key(std::string_view file) {
    std::string key{file};
    for (char& letter : key) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return key;
}

MapMemory capture_map_memory(const world::MapSession& session, const Battle& battle,
                             const std::set<int>& opened_chests, std::int64_t day) {
    MapMemory memory;
    memory.opened_chests = opened_chests;
    memory.remembered_day = day;
    for (const auto& door : session.doors) {
        if (door.open) {
            memory.open_doors.push_back(door.id);
        }
    }
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        if (!battle.alive(i)) {
            memory.dead.push_back(i);
        }
    }
    return memory;
}

MapMemories load_map_memories(std::span<const SaveState::RememberedMap> records) {
    MapMemories memories;
    for (const auto& record : records) {
        memories[map_memory_key(record.file)] = {
            record.opened_chests,
            record.open_doors,
            record.dead,
            record.day,
        };
    }
    return memories;
}

std::vector<SaveState::RememberedMap> save_map_memories(const MapMemories& memories,
                                                        std::string_view active_file,
                                                        const MapMemory& active) {
    std::vector<SaveState::RememberedMap> records;
    const auto active_key = map_memory_key(active_file);
    for (const auto& [file, memory] : memories) {
        const auto key = map_memory_key(file);
        if (key != active_key) {
            records.push_back(
                {key, memory.remembered_day, memory.opened_chests, memory.open_doors, memory.dead});
        }
    }
    records.push_back(
        {active_key, active.remembered_day, active.opened_chests, active.open_doors, active.dead});
    return records;
}

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
