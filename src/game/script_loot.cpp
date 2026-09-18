#include "game/script_loot.hpp"

#include <algorithm>
#include <cmath>

#include "game/script_objects.hpp"
#include "game/temporary_objects.hpp"

namespace starhaven::game {

std::string_view loot_spawn_error_name(LootSpawnError error) noexcept {
    switch (error) {
    case LootSpawnError::None:
        return "none";
    case LootSpawnError::UnsupportedId:
        return "unsupported_id";
    case LootSpawnError::MissingResource:
        return "missing_resource";
    }
    return "unknown";
}

bool valid_script_loot(const ScriptLootState& state, const world::MapSession& session,
                       const data::ItemStatsTable& items) {
    if (state.objects.size() > kTemporaryObjectCapacity || !std::isfinite(state.tick_remainder) ||
        state.tick_remainder < 0 || state.tick_remainder >= 1)
        return false;
    world::ObjectSpawnRequest request;
    request.object_id = 1;
    const auto resources =
        resolve_object_spawn(request, session.object_descriptors.entries(), items.entries());
    for (const auto& loot : state.objects) {
        const auto* descriptor = session.object_descriptors.at(loot.descriptor);
        if (!resources || descriptor == nullptr || loot.descriptor == 0 ||
            loot.descriptor != resources->descriptor_index || loot.item_id != resources->item_id ||
            loot.item_id <= 0 || descriptor->flags != 0 ||
            descriptor->sprite_frame_index >= session.sprite_frames.size() ||
            !session.sprite_frames.frames()[descriptor->sprite_frame_index].starts_group())
            return false;
        for (const auto value : {
                 loot.position.x,
                 loot.position.y,
                 loot.position.z,
                 loot.velocity.x,
                 loot.velocity.y,
                 loot.velocity.z,
             }) {
            if (!std::isfinite(value))
                return false;
        }
    }
    return true;
}

LootSpawnResult spawn_script_loot(const world::ObjectSpawnRequest& request,
                                  const world::MapSession& session,
                                  const data::ItemStatsTable& items, ScriptLootState& state,
                                  std::size_t occupied) {
    LootSpawnResult result;
    if (request.object_id != 1) {
        result.error = LootSpawnError::UnsupportedId;
        return result;
    }
    const auto resources =
        resolve_object_spawn(request, session.object_descriptors.entries(), items.entries());
    if (!resources || resources->descriptor_index == 0 || resources->item_id <= 0) {
        result.error = LootSpawnError::MissingResource;
        return result;
    }
    const auto& descriptor = session.object_descriptors.entries()[resources->descriptor_index];
    if (descriptor.flags != 0 || descriptor.sprite_frame_index >= session.sprite_frames.size() ||
        !session.sprite_frames.frames()[descriptor.sprite_frame_index].starts_group()) {
        result.error = LootSpawnError::MissingResource;
        return result;
    }
    Mm6Random random{state.random};
    const auto placed = static_cast<std::size_t>(std::ranges::count_if(
        session.objects, [](const auto& object) { return object.descriptor_index != 0; }));
    const auto available = kTemporaryObjectCapacity - std::min(kTemporaryObjectCapacity, placed);
    const auto capacity = available - std::min(available, occupied);
    for (std::size_t attempt = 0; attempt < request.count; ++attempt) {
        std::uint16_t yaw = 0;
        std::uint16_t pitch = 512;
        if (request.scatter) {
            yaw = random.next() % 2048;
            pitch = static_cast<std::uint16_t>(256 + (random.next() % 512) / 2);
        }
        if (state.objects.size() >= capacity) {
            ++result.dropped;
            continue;
        }
        state.objects.push_back({
            resources->descriptor_index,
            resources->item_id,
            world::to_render_space(request.x, request.y, request.z),
            object_launch_velocity(request.speed, yaw, pitch),
            false,
        });
        ++result.created;
    }
    state.random = random.state();
    return result;
}

void advance_script_loot(ScriptLootState& state, double seconds, const world::MapSession& session) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    // Same bounded frame quantum as the live simulation; excess elapsed time
    // is discarded rather than allowing an unbounded catch-up on a bad frame.
    const double elapsed = state.tick_remainder + std::min(seconds, 1.0) * kObjectTicksPerSecond;
    const auto ticks = static_cast<std::uint32_t>(elapsed);
    state.tick_remainder = elapsed - ticks;
    for (auto& loot : state.objects) {
        const auto* descriptor = session.object_descriptors.at(loot.descriptor);
        if (descriptor == nullptr || descriptor->object_id != 1 || descriptor->flags != 0)
            continue;
        TemporaryObject motion;
        motion.definition.radius = static_cast<float>(descriptor->radius);
        motion.position = loot.position;
        motion.velocity = loot.velocity;
        motion.resting = loot.resting;
        for (std::uint32_t tick = 0; tick < ticks; ++tick)
            advance_object_motion(motion, session.collision,
                                  session.outdoor() ? &session.terrain : nullptr);
        loot.position = motion.position;
        loot.velocity = motion.velocity;
        loot.resting = motion.resting;
    }
}

std::vector<int> take_script_loot(ScriptLootState& state, const world::MapSession& session,
                                  const data::ItemStatsTable& items, assets::AssetCache& cache,
                                  render::Vec3 party, std::span<Pack> packs) {
    std::vector<int> taken;
    std::erase_if(state.objects, [&](const ScriptLootObject& loot) {
        const auto* row =
            loot.item_id > 0 ? items.at(static_cast<std::size_t>(loot.item_id)) : nullptr;
        const auto delta = loot.position - party;
        if (row == nullptr || delta.x * delta.x + delta.z * delta.z > kPickUpRange * kPickUpRange ||
            std::abs(delta.y) > kPickUpHeight)
            return false;
        // Aim above the ground so the supporting floor is not an occluder.
        const auto target = loot.position + render::Vec3{0, 1, 0};
        if (session.collision.sweep_sphere(party, target, 0))
            return false;
        const auto& icon = cache.icon(row->picture);
        const int width = std::max(1, cells_across(icon.width()));
        const int height = std::max(1, cells_across(icon.height()));
        for (auto& pack : packs) {
            if (pack.add(loot.item_id, width, height, false)) {
                taken.push_back(loot.item_id);
                return true;
            }
        }
        return false;
    });
    return taken;
}

}  // namespace starhaven::game
