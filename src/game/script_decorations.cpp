#include "game/script_decorations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/world/map_session.hpp"
#include "game/script_walk.hpp"

namespace starhaven::game {
namespace {

void set_descriptor(world::SessionDecoration& decoration, const world::DecorationTable& table,
                    std::uint16_t descriptor) {
    const auto* type = table.at(descriptor);
    decoration.descriptor_id = descriptor;
    decoration.name = type == nullptr ? std::string{} : type->name;
    decoration.sound_id = type == nullptr ? 0 : type->sound_id;
    decoration.radius = type == nullptr ? 0 : type->radius;
    decoration.descriptor_flags = type == nullptr ? 0 : type->flags;
}

void set_visible(world::SessionDecoration& decoration, bool visible) {
    if (visible) {
        decoration.flags &= static_cast<std::uint16_t>(~0x20U);
    } else {
        decoration.flags |= 0x20U;
    }
}

}  // namespace

bool has_decoration_event(std::uint16_t descriptor) noexcept {
    constexpr auto kIds = std::to_array<std::uint16_t>(
        {118, 119, 120, 121, 146, 154, 155, 158, 162, 163, 164, 166, 167, 182});
    return std::ranges::find(kIds, descriptor) != kIds.end();
}

std::uint8_t initial_decoration_event(std::uint16_t descriptor, Mm6Random& random) {
    const int roll = random.next() % 100;
    // Event bands and thresholds from the observed initial-state routine.
    switch (descriptor) {
    case 118:
    case 119:
    case 120:
    case 121:
        return roll < 50 ? 47 : static_cast<std::uint8_t>(48 + random.next() % 10);
    case 146:
        return roll < 20 ? 37 : 36;
    case 154:
        if (roll < 40)
            return 31;
        if (roll < 70)
            return 32;
        return roll < 90 ? 33 : 34;
    case 155:
        return static_cast<std::uint8_t>(43 + roll / 25);
    case 158:
        return roll < 50 ? 29 : 30;
    case 162:
        return 35;
    case 163:
    case 164:
        return static_cast<std::uint8_t>(10 + std::max(0, roll - 20) / 10);
    case 166:
        if (roll < 40)
            return 39;
        if (roll < 70)
            return 40;
        return roll < 90 ? 41 : 42;
    case 167:
        if (roll < 80)
            return 25;
        if (roll < 90)
            return 26;
        return roll < 97 ? 27 : 28;
    case 182:
        return roll < 20 ? 19 : 23;
    default:
        return 0;
    }
}

void initialize_decoration_events(world::MapSession& session) {
    if (session.decoration_events_ready)
        return;
    session.decoration_events_ready = true;
    // Stable map-specific engine seed, independent of the original global RNG.
    std::uint32_t seed = 2166136261U;
    for (const unsigned char c : script_scope(session.file_name)) {
        seed = (seed ^ c) * 16777619U;
    }
    Mm6Random random{seed};
    std::size_t count = 0;
    for (auto& decoration : session.decorations) {
        if (decoration.event_id != 0 || !has_decoration_event(decoration.descriptor_id))
            continue;
        if (count++ >= 124)
            break;
        decoration.event_value = initial_decoration_event(decoration.descriptor_id, random);
    }
}

std::optional<DecorationInteraction> decoration_interaction(const world::MapSession& session,
                                                            std::uint32_t index) {
    if (index >= session.decorations.size())
        return std::nullopt;
    const auto& decoration = session.decorations[index];
    if (!decoration.active())
        return std::nullopt;
    if (decoration.event_id != 0) {
        return DecorationInteraction{index, decoration.event_id, false, 0};
    }
    if (!has_decoration_event(decoration.descriptor_id) || !decoration.event_value)
        return std::nullopt;
    return DecorationInteraction{
        index,
        static_cast<std::uint16_t>(400 + *decoration.event_value),
        true,
        0,
    };
}

std::optional<DecorationInteraction> aimed_decoration(const world::MapSession& session,
                                                      render::Vec3 eye, render::Vec3 forward,
                                                      float range) {
    std::optional<DecorationInteraction> result;
    for (std::size_t i = 0; i < session.decorations.size(); ++i) {
        auto candidate = decoration_interaction(session, static_cast<std::uint32_t>(i));
        if (!candidate)
            continue;
        const auto& decoration = session.decorations[i];
        if (!decoration.visible())
            continue;
        auto target = decoration.position;
        const auto* type = session.decoration_types.at(decoration.descriptor_id);
        target.y += type == nullptr ? 32.0f : static_cast<float>(type->height) / 2.0f;
        const auto delta = target - eye;
        const float distance = std::sqrt(render::dot(delta, delta));
        if (distance < 0.001f || distance > range ||
            render::dot(delta, forward) / distance < 0.978f)
            continue;
        bool blocked = false;
        for (const auto& polygon : session.collision.polygons()) {
            const float denominator = render::dot(polygon.normal, delta);
            if (std::abs(denominator) < 0.0001f)
                continue;
            const float fraction =
                -(render::dot(polygon.normal, eye) + polygon.distance) / denominator;
            if (fraction > 0.001f && fraction < 0.999f &&
                world::point_in_polygon(polygon, eye + delta * fraction)) {
                blocked = true;
                break;
            }
        }
        if (blocked)
            continue;
        candidate->distance = distance;
        range = distance;
        result = candidate;
    }
    return result;
}

std::size_t apply_script_decorations(world::MapSession& session,
                                     std::span<const world::DecorationChange> changes,
                                     DecorationChanges& memory) {
    std::size_t applied = 0;
    for (const auto& change : changes) {
        if (change.index >= session.decorations.size()) {
            continue;
        }
        auto& decoration = session.decorations[change.index];
        if (change.event) {
            if (!decoration.event_value)
                continue;
            decoration.event_value =
                *change.event == 0 ? std::uint8_t{0}
                                   : static_cast<std::uint8_t>((*change.event + 112U) & 0xffU);
            if (*change.event == 0)
                set_visible(decoration, false);
        } else {
            if (change.name != "0") {
                const auto* type = session.decoration_types.find(change.name);
                const auto index = type == nullptr
                                       ? std::size_t{0}
                                       : static_cast<std::size_t>(
                                             type - session.decoration_types.entries().data());
                const auto descriptor = index > std::numeric_limits<std::uint16_t>::max()
                                            ? std::uint16_t{0}
                                            : static_cast<std::uint16_t>(index);
                // The original resolves unknown names to descriptor zero.
                set_descriptor(decoration, session.decoration_types, descriptor);
            }
            set_visible(decoration, change.visible);
        }
        memory[script_scope(session.file_name)][change.index] = {
            decoration.descriptor_id,
            decoration.active(),
            decoration.event_value,
        };
        ++applied;
    }
    return applied;
}

void restore_script_decorations(world::MapSession& session, const DecorationChanges& memory) {
    initialize_decoration_events(session);
    const auto found = memory.find(script_scope(session.file_name));
    if (found == memory.end()) {
        return;
    }
    for (const auto& [index, state] : found->second) {
        if (index >= session.decorations.size()) {
            continue;
        }
        auto& decoration = session.decorations[index];
        set_descriptor(decoration, session.decoration_types, state.descriptor);
        set_visible(decoration, state.visible);
        if (state.event_value) {
            decoration.event_value = state.event_value;
            if (*state.event_value == 0 && decoration.event_id == 0 &&
                has_decoration_event(decoration.descriptor_id)) {
                set_visible(decoration, false);
            }
        }
    }
}

}  // namespace starhaven::game
