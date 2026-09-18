#include "game/temporary_objects.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace starhaven::game {
namespace {

constexpr std::uint16_t kTemporary = 0x04;
constexpr std::uint16_t kNoGravity = 0x20;
constexpr float kStepSeconds = 1.0f / kObjectTicksPerSecond;
constexpr float kSeparation = 0.01f;

TemporarySpawnError definition_for(std::uint16_t id,
                                   std::span<const world::ObjectDescriptor> objects,
                                   std::span<const world::SpriteFrame> frames,
                                   TemporaryObjectDefinition& out) {
    const auto found = std::ranges::find(objects, id, &world::ObjectDescriptor::object_id);
    if (found == objects.end() || found == objects.begin() ||
        !std::in_range<std::uint16_t>(found - objects.begin()) ||
        (found->flags & kTemporary) == 0) {
        return TemporarySpawnError::MissingDescriptor;
    }
    if (found->sprite_frame_index >= frames.size() ||
        !frames[found->sprite_frame_index].starts_group()) {
        return TemporarySpawnError::BadFrame;
    }
    // DOBJLIST.BIN already stores animation-derived lifetime in 128 Hz ticks;
    // the text-table builder, not the runtime, multiplied group length by 8.
    out = {
        id,
        static_cast<std::uint16_t>(found - objects.begin()),
        found->sprite_frame_index,
        found->flags,
        std::min(std::uint32_t{found->lifetime}, 32768U),
        static_cast<float>(found->radius),
    };
    return TemporarySpawnError::None;
}

// Quantize trigonometry independently. Exact legacy lookup-table rounding is
// not asserted; fixed products and the signed low-word storage are explicit.
std::int64_t fixed_product(std::int64_t a, std::int64_t b) {
    const auto product = a * b;
    return product >= 0 ? product / 65536 : -((-product + 65535) / 65536);
}

render::Vec3 launch_velocity(std::int32_t speed, std::uint16_t yaw, std::uint16_t pitch) {
    const auto angle = [](std::uint16_t units) {
        return static_cast<double>(units) * (2.0 * std::numbers::pi / 2048.0);
    };
    const auto sine = [&](std::uint16_t units) {
        return std::llround(std::sin(angle(units)) * 65536);
    };
    const auto cosine = [&](std::uint16_t units) {
        return std::llround(std::cos(angle(units)) * 65536);
    };
    const auto component = [speed](std::int64_t direction) {
        const auto low = static_cast<std::uint16_t>(fixed_product(speed, direction));
        return static_cast<float>(std::bit_cast<std::int16_t>(low));
    };
    return {
        component(fixed_product(cosine(yaw), cosine(pitch))),
        component(sine(pitch)),
        component(fixed_product(sine(yaw), cosine(pitch))),
    };
}

void finish(TemporaryObject& object, TemporaryObjectStep& result) {
    // ID 8080's non-actor contact/expiry path only removes the object. Its
    // resisted actor effect and 8081 transition require character integration.
    // The original impact path discards objects 5020 units from their origin.
    if ((object.definition.id == 1050 || object.definition.id == 2100 ||
         object.definition.id == 4070) &&
        (object.definition.flags & 0x40U) != 0 && object.impact_definition &&
        render::length(object.position - object.origin) < 5020.0f) {
        object.definition = *object.impact_definition;
        object.impact_definition.reset();
        object.age = 0;
        object.velocity = {};
        object.resting = true;
        result.detonations.push_back({object.position, 512});
    } else {
        object.active = false;
        ++result.expired;
    }
}

void move_one_tick(TemporaryObject& object, const world::CollisionWorld& collision,
                   TemporaryObjectStep& result, const world::OdmTerrain* terrain) {
    const render::Vec3 offset{0, object.definition.radius + 1, 0};
    if (object.resting) {
        const auto center = object.position + offset;
        if (const auto support = collision.sweep_sphere(center, center - render::Vec3{0, 0.1f, 0},
                                                        object.definition.radius, terrain);
            support && support->normal.y > world::kFloorNormalY) {
            return;
        }
        object.resting = false;
    }
    if ((object.definition.flags & kNoGravity) == 0) {
        object.velocity.y -= 5.0f;
    }
    float remaining = kStepSeconds;
    // Bound corner/overlap recovery; unconsumed movement is discarded after
    // four contacts in one tick, an explicit engine stability policy.
    for (int contacts = 0; contacts < 4 && remaining > 0; ++contacts) {
        const auto center = object.position + offset;
        const auto target = center + object.velocity * remaining;
        const auto hit = collision.sweep_sphere(center, target, object.definition.radius, terrain);
        if (!hit) {
            object.position = target - offset;
            return;
        }
        result.terrain_contacts += hit->terrain ? 1 : 0;
        object.position = hit->position - offset + hit->normal * hit->penetration;
        // ID 4070's impact action returns to ordinary geometry response.
        // Its common displacement cutoff still runs before that exception.
        if ((object.definition.flags & 0x40U) != 0 &&
            (object.definition.id != 4070 ||
             render::length(object.position - object.origin) >= 5020.0f)) {
            finish(object, result);
            return;
        }
        object.position = object.position + hit->normal * kSeparation;
        const float inward = render::dot(object.velocity, hit->normal);
        if (inward < 0) {
            ++result.bounces;
            if (hit->normal.y > world::kFloorNormalY && object.velocity.y < 0) {
                object.velocity.y =
                    (object.definition.flags & 0x80U) != 0 ? -object.velocity.y * 0.5f : 0.0f;
                if (object.velocity.y < 10) {
                    object.velocity.y = 0;
                }
                constexpr float kDamping = 58500.0f / 65536.0f;
                object.velocity.x *= kDamping;
                object.velocity.z *= kDamping;
                if (object.velocity.x * object.velocity.x + object.velocity.z * object.velocity.z <
                    400) {
                    object.velocity.x = object.velocity.z = 0;
                }
            } else {
                object.velocity = object.velocity - hit->normal * (2 * inward);
            }
        }
        if (render::dot(object.velocity, object.velocity) == 0) {
            object.resting = true;
            return;
        }
        remaining *= 1 - hit->fraction;
    }
}

}  // namespace

render::Vec3 object_launch_velocity(std::int32_t speed, std::uint16_t yaw, std::uint16_t pitch) {
    return launch_velocity(speed, yaw, pitch);
}

void advance_object_motion(TemporaryObject& object, const world::CollisionWorld& collision,
                           const world::OdmTerrain* terrain) {
    TemporaryObjectStep ignored;
    object.previous = object.position;
    move_one_tick(object, collision, ignored, terrain);
}

TemporarySpawnResult TemporaryObjects::spawn(const world::ObjectSpawnRequest& request,
                                             std::span<const world::ObjectDescriptor> objects,
                                             std::span<const world::SpriteFrame> frames,
                                             Mm6Random& random, std::size_t occupied) {
    TemporarySpawnResult result;
    // Match full requests, not a truncated alias of a supported ID. ID 2081
    // is launched normally: its descriptor disables gravity and supplies its
    // animation lifetime, without an impact action or replacement.
    if (request.object_id != 1000 && request.object_id != 1050 && request.object_id != 2081 &&
        request.object_id != 2100 && request.object_id != 4070 && request.object_id != 8080) {
        result.error = TemporarySpawnError::UnsupportedId;
        return result;
    }
    TemporaryObject prototype;
    result.error = definition_for(static_cast<std::uint16_t>(request.object_id), objects, frames,
                                  prototype.definition);
    if (result.error != TemporarySpawnError::None) {
        return result;
    }
    if (request.object_id == 1050 || request.object_id == 2100 || request.object_id == 4070) {
        TemporaryObjectDefinition impact;
        result.error = definition_for(static_cast<std::uint16_t>(request.object_id + 1), objects,
                                      frames, impact);
        if (result.error != TemporarySpawnError::None) {
            return result;
        }
        prototype.impact_definition = impact;
    }
    prototype.position = {
        static_cast<float>(request.x),
        static_cast<float>(request.z),
        static_cast<float>(request.y),
    };
    prototype.origin = prototype.previous = prototype.position;
    const auto capacity = kTemporaryObjectCapacity - std::min(occupied, kTemporaryObjectCapacity);
    auto active = active_count();
    for (std::size_t attempt = 0; attempt < request.count; ++attempt) {
        std::uint16_t yaw = 0;
        std::uint16_t pitch = 512;
        if (request.scatter) {
            yaw = random.next() % 2048;
            pitch = static_cast<std::uint16_t>(256 + (random.next() % 512) / 2);
        }
        const auto free = std::ranges::find(objects_, false, &TemporaryObject::active);
        if (active >= capacity) {
            ++result.dropped;
            continue;
        }
        prototype.velocity = launch_velocity(request.speed, yaw, pitch);
        if (free == objects_.end()) {
            objects_.push_back(prototype);
        } else {
            *free = prototype;
        }
        ++result.created;
        ++active;
    }
    return result;
}

TemporaryObjectStep TemporaryObjects::advance(std::uint32_t ticks,
                                              const world::CollisionWorld& collision,
                                              const world::OdmTerrain* terrain) {
    TemporaryObjectStep result;
    for (auto& object : objects_) {
        for (std::uint32_t tick = 0; tick < ticks && object.active; ++tick) {
            object.previous = object.position;
            ++object.age;
            if (object.age >= object.definition.lifetime) {
                finish(object, result);
                continue;
            }
            if (object.definition.id != 1051 && object.definition.id != 2101 &&
                object.definition.id != 4071) {
                move_one_tick(object, collision, result, terrain);
            }
        }
    }
    return result;
}

std::size_t TemporaryObjects::active_count() const noexcept {
    return static_cast<std::size_t>(std::ranges::count(objects_, true, &TemporaryObject::active));
}

}  // namespace starhaven::game
