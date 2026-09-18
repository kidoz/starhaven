#ifndef STARHAVEN_GAME_SCRIPT_LOOT_HPP
#define STARHAVEN_GAME_SCRIPT_LOOT_HPP

#include <span>
#include <string_view>
#include <vector>

#include "core/world/map_session.hpp"
#include "game/inventory.hpp"

namespace starhaven::game {

// Persistent event-created loot only. Resource-derived names, frames and
// dimensions are resolved from the current installation, never stored in saves.
struct ScriptLootObject {
    std::uint16_t descriptor = 0;
    int item_id = 0;
    render::Vec3 position;
    render::Vec3 velocity;
    bool resting = false;
};

struct ScriptLootState {
    std::uint32_t random = 1;  // per-map event objects, independent of other gameplay RNGs
    double tick_remainder = 0;
    std::vector<ScriptLootObject> objects;
};

enum class LootSpawnError : std::uint8_t {
    None,
    UnsupportedId,
    MissingResource,
};
[[nodiscard]] std::string_view loot_spawn_error_name(LootSpawnError error) noexcept;

struct LootSpawnResult {
    LootSpawnError error = LootSpawnError::None;
    std::size_t created = 0;
    std::size_t dropped = 0;
};

// ID 1 is the persistent loot requested by shipped CD2 events. Other object
// families remain explicit errors until their complete lifecycle is supported.
[[nodiscard]] LootSpawnResult spawn_script_loot(const world::ObjectSpawnRequest& request,
                                                const world::MapSession& session,
                                                const data::ItemStatsTable& items,
                                                ScriptLootState& state, std::size_t occupied = 0);
[[nodiscard]] bool valid_script_loot(const ScriptLootState& state, const world::MapSession& session,
                                     const data::ItemStatsTable& items);
void advance_script_loot(ScriptLootState& state, double seconds, const world::MapSession& session);

// Return item IDs successfully picked up. Full packs, out-of-range objects,
// and objects behind collision geometry stay in the world.
[[nodiscard]] std::vector<int> take_script_loot(ScriptLootState& state,
                                                const world::MapSession& session,
                                                const data::ItemStatsTable& items,
                                                assets::AssetCache& cache, render::Vec3 party,
                                                std::span<Pack> packs);

}  // namespace starhaven::game
#endif
