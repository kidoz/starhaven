#include "game/script_object_effects.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace starhaven::game {

LootSpawnResult ScriptObjectEffects::spawn(const world::ObjectSpawnRequest& request,
                                           const world::MapSession& session,
                                           const data::ItemStatsTable& items,
                                           ScriptLootState& loot) {
    if (request.object_id == 1)
        return spawn_script_loot(request, session, items, loot, temporary_.active_count());
    const auto placed = static_cast<std::size_t>(std::ranges::count_if(
        session.objects, [](const auto& object) { return object.descriptor_index != 0; }));
    // Saturate before adding, including when presented with invalid caller state.
    const auto reserved = std::min(placed, kTemporaryObjectCapacity);
    const auto occupied =
        reserved + std::min(loot.objects.size(), kTemporaryObjectCapacity - reserved);
    Mm6Random random{loot.random};
    const auto result = temporary_.spawn(request, session.object_descriptors.entries(),
                                         session.sprite_frames.frames(), random, occupied);
    loot.random = random.state();
    auto error = LootSpawnError::None;
    if (result.error == TemporarySpawnError::UnsupportedId)
        error = LootSpawnError::UnsupportedId;
    else if (result.error != TemporarySpawnError::None)
        error = LootSpawnError::MissingResource;
    return {error, result.created, result.dropped};
}

TemporaryObjectStep ScriptObjectEffects::advance(double seconds, const world::MapSession& session) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return {};
    const double elapsed = tick_remainder_ + std::min(seconds, 1.0) * kObjectTicksPerSecond;
    const auto ticks = static_cast<std::uint32_t>(elapsed);
    tick_remainder_ = elapsed - ticks;
    return temporary_.advance(ticks, session.collision,
                              session.outdoor() ? &session.terrain : nullptr);
}

std::vector<ActiveLaunch>
ScriptObjectEffects::sprites(const world::SpriteFrameTable& frames) const {
    std::vector<ActiveLaunch> result;
    result.reserve(temporary_.active_count());
    for (const auto& object : temporary_.slots()) {
        if (!object.active || (object.definition.flags & 1U) != 0 ||
            object.definition.frame >= frames.size())
            continue;
        ActiveLaunch sprite;
        sprite.animation = frames.frames()[object.definition.frame].group_name;
        if (sprite.animation.empty())
            continue;
        sprite.position = object.position;
        // DOBJ animation lifetimes are DSFT group length * 8 simulation ticks.
        sprite.animation_ticks = object.age / 8;
        result.push_back(std::move(sprite));
    }
    return result;
}

void ScriptObjectEffects::clear() noexcept {
    temporary_.clear();
    tick_remainder_ = 0;
}

}  // namespace starhaven::game
