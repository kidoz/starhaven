#include "game/script_object_effects.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "game/combat.hpp"
#include "game/player.hpp"

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

std::uint32_t ScriptObjectEffects::elapsed_ticks(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return 0;
    const double elapsed = tick_remainder_ + std::min(seconds, 1.0) * kObjectTicksPerSecond;
    const auto ticks = static_cast<std::uint32_t>(elapsed);
    tick_remainder_ = elapsed - ticks;
    return ticks;
}

TemporaryObjectStep ScriptObjectEffects::advance(double seconds, const world::MapSession& session) {
    return temporary_.advance(elapsed_ticks(seconds), session.collision,
                              session.outdoor() ? &session.terrain : nullptr);
}

TemporaryObjectStep ScriptObjectEffects::advance(double seconds, const world::MapSession& session,
                                                 Battle& battle,
                                                 const data::MonsterStatsTable& monsters,
                                                 ScriptLootState& loot,
                                                 std::optional<render::Vec3> party_eye) {
    const auto ticks = elapsed_ticks(seconds);
    if (ticks == 0 || temporary_.active_count() == 0)
        return {};
    std::vector<ObjectActor> bodies;
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        const auto& actor = session.actors[i];
        const auto id = static_cast<std::size_t>(actor.monster_id);
        if (!battle.alive(i) || actor.monster_id <= 0 || id > monsters.size())
            continue;
        const auto* body = session.monsters.at(id - 1);
        // Same fallback body as aiming; invalid/missing stats are never targets.
        bodies.push_back({
            i,
            actor.position,
            body != nullptr && body->radius > 0 ? static_cast<float>(body->radius) : 48.0f,
            body != nullptr && body->height > 0 ? static_cast<float>(body->height) : 160.0f,
        });
    }
    Mm6Random random{loot.random};
    ObjectContacts contacts{
        bodies,
        [&](std::size_t actor) {
            const auto id = static_cast<std::size_t>(session.actors[actor].monster_id);
            return battle.accept_event_object_8080(actor, monsters.entries()[id - 1], random);
        },
    };
    std::size_t animation_fallbacks = 0;
    contacts.react_2100 = [&](std::size_t actor) {
        const auto id = static_cast<std::size_t>(session.actors[actor].monster_id);
        const auto* body = session.monsters.at(id - 1);
        const auto group =
            body != nullptr
                ? session.sprite_frames.group(body->animation(world::MonsterAnimation::Wince))
                : std::span<const world::SpriteFrame>{};
        float seconds = kWinceSeconds;
        // Original action length is group length * 8 in a signed word. Reject
        // absent/zero/overflowing data and retain the existing engine fallback.
        if (!group.empty() && group.front().group_length > 0 && group.front().group_length < 4096)
            seconds = static_cast<float>(group.front().group_length) / 16.0f;
        else
            ++animation_fallbacks;
        battle.react_to_event_object_2100(actor, seconds);
    };
    if (party_eye)
        contacts.party =
            ObjectParty{*party_eye - render::Vec3{0, kEyeHeight, 0}, kBodyRadius, kBodyHeight};
    auto result = temporary_.advance(ticks, session.collision,
                                     session.outdoor() ? &session.terrain : nullptr, &contacts);
    loot.random = random.state();
    result.actor_animation_fallbacks = animation_fallbacks;
    return result;
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
