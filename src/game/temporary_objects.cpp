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
    object.touching_actor.reset();
    // ID 8080's non-actor contact/expiry path only removes the object.
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

// Sweep a point against a cylinder expanded by the projectile radius. The
// flat expanded end caps are deliberately conservative at the body's corners.
std::optional<float> body_fraction(render::Vec3 from, render::Vec3 to, float radius,
                                   render::Vec3 feet, float body_radius, float body_height) {
    if (body_radius <= 0 || body_height <= 0)
        return std::nullopt;
    const auto delta = to - from;
    const auto relative = from - feet;
    const float reach = radius + body_radius;
    float enter = 0;
    float leave = 1;
    const float a = delta.x * delta.x + delta.z * delta.z;
    const float b = relative.x * delta.x + relative.z * delta.z;
    const float c = relative.x * relative.x + relative.z * relative.z - reach * reach;
    if (a == 0) {
        if (c > 0)
            return std::nullopt;
    } else {
        const float discriminant = b * b - a * c;
        if (discriminant < 0)
            return std::nullopt;
        const float root = std::sqrt(discriminant);
        enter = std::max(enter, (-b - root) / a);
        leave = std::min(leave, (-b + root) / a);
    }
    const float bottom = -radius;
    const float top = body_height + radius;
    if (delta.y == 0) {
        if (relative.y < bottom || relative.y > top)
            return std::nullopt;
    } else {
        const float first = (bottom - relative.y) / delta.y;
        const float second = (top - relative.y) / delta.y;
        enter = std::max(enter, std::min(first, second));
        leave = std::min(leave, std::max(first, second));
    }
    return enter <= leave ? std::optional{enter} : std::nullopt;
}

void contact_actor(TemporaryObject& object, TemporaryObjectStep& result,
                   const ObjectContacts& actors, const ObjectActor& actor) {
    ++result.actor_contacts;
    if (object.definition.id == 4070) {
        // Source-zero event objects transform without resistance or target damage.
        finish(object, result);
        return;
    }
    if (object.definition.id == 2100) {
        if (render::length(object.position - object.origin) >= 5020.0f) {
            finish(object, result);
            return;
        }
        // Redirect away from the actor, preserving horizontal speed before
        // the original common 58500/65536 damping of all three components.
        const float speed = std::hypot(object.velocity.x, object.velocity.z);
        const render::Vec3 outward{
            object.position.x - actor.position.x,
            0,
            object.position.z - actor.position.z,
        };
        const float distance = render::length(outward);
        const auto direction = distance > 0 ? outward * (1.0f / distance) : render::Vec3{1, 0, 0};
        object.velocity.x = direction.x * speed;
        object.velocity.z = direction.z * speed;
        object.velocity = object.velocity * (58500.0f / 65536.0f);
        object.touching_actor = actor.index;
        object.resting = false;
        ++result.actor_redirects;
        if (actors.react_2100)
            actors.react_2100(actor.index);
        return;
    }
    // The common displacement guard runs before the resistance draw.
    if (render::length(object.position - object.origin) >= 5020.0f || !actors.apply(actor.index)) {
        finish(object, result);
        return;
    }
    ++result.actor_accepted;
    if (!object.impact_definition) {
        ++result.missing_actor_replacements;
        finish(object, result);
        return;
    }
    object.definition = *object.impact_definition;
    object.impact_definition.reset();
    object.age = 0;
    object.velocity = {};
    object.resting = true;
}

// Geometry wins ties, then actor order, then party. Bodies are fixed for the
// caller's simulation step; original moving-target/sector parity is not claimed.
std::optional<float> contact_characters(TemporaryObject& object, render::Vec3 from, render::Vec3 to,
                                        float nearest, TemporaryObjectStep& result,
                                        const ObjectContacts* contacts) {
    if (contacts == nullptr || (object.definition.flags & 0x40U) == 0 ||
        (object.definition.id != 2100 && object.definition.id != 4070 &&
         object.definition.id != 8080))
        return std::nullopt;
    const ObjectActor* selected = nullptr;
    bool party_hit = false;
    if (object.definition.id != 8080 || contacts->apply) {
        for (const auto& actor : contacts->bodies) {
            if (object.definition.id == 2100 && object.touching_actor == actor.index)
                continue;
            if (const auto fraction = body_fraction(from, to, object.definition.radius,
                                                    actor.position, actor.radius, actor.height);
                fraction && *fraction < nearest) {
                nearest = *fraction;
                selected = &actor;
            }
        }
    }
    if (contacts->party) {
        const auto& party = *contacts->party;
        if (const auto fraction = body_fraction(from, to, object.definition.radius, party.position,
                                                party.radius, party.height);
            fraction && *fraction < nearest) {
            nearest = *fraction;
            party_hit = true;
        }
    }
    if (!party_hit && selected == nullptr)
        return std::nullopt;
    const render::Vec3 offset{0, object.definition.radius + 1, 0};
    object.position = from + (to - from) * nearest - offset;
    if (party_hit) {
        ++result.party_contacts;
        finish(object, result);
    } else {
        contact_actor(object, result, *contacts, *selected);
    }
    return nearest;
}

void move_one_tick(TemporaryObject& object, const world::CollisionWorld& collision,
                   TemporaryObjectStep& result, const world::OdmTerrain* terrain,
                   const ObjectContacts* contacts = nullptr) {
    const render::Vec3 offset{0, object.definition.radius + 1, 0};
    if (object.touching_actor) {
        const ObjectActor* body = nullptr;
        if (contacts != nullptr) {
            for (const auto& actor : contacts->bodies)
                if (actor.index == *object.touching_actor)
                    body = &actor;
        }
        const auto center = object.position + offset;
        if (body == nullptr || !body_fraction(center, center, object.definition.radius,
                                              body->position, body->radius, body->height))
            object.touching_actor.reset();
    }
    if (object.resting) {
        const auto center = object.position + offset;
        if (const auto support = collision.sweep_sphere(center, center - render::Vec3{0, 0.1f, 0},
                                                        object.definition.radius, terrain);
            support && support->normal.y > world::kFloorNormalY) {
            // A settled 4070 can still be touched by a body entering its space.
            (void)contact_characters(object, center, center, 2.0f, result, contacts);
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
    for (int contact_count = 0; contact_count < 4 && remaining > 0; ++contact_count) {
        const auto center = object.position + offset;
        const auto target = center + object.velocity * remaining;
        const auto hit = collision.sweep_sphere(center, target, object.definition.radius, terrain);
        if (const auto fraction = contact_characters(
                object, center, target, hit ? hit->fraction : 2.0f, result, contacts)) {
            if (!object.active || object.definition.id != 2100)
                return;
            remaining *= 1 - *fraction;
            continue;
        }
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
    if (request.object_id == 8080) {
        TemporaryObjectDefinition impact;
        // Non-actor removal is valid without 8081. A missing/bad replacement
        // removes an accepted actor hit after applying its state response.
        if (definition_for(8081, objects, frames, impact) == TemporarySpawnError::None)
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
                                              const world::OdmTerrain* terrain,
                                              const ObjectContacts* contacts) {
    TemporaryObjectStep result;
    // Tick order keeps shared resistance draws independent of frame batching.
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        bool any_active = false;
        for (auto& object : objects_) {
            if (!object.active)
                continue;
            any_active = true;
            object.previous = object.position;
            ++object.age;
            if (object.age >= object.definition.lifetime) {
                finish(object, result);
                continue;
            }
            if (object.definition.id != 1051 && object.definition.id != 2101 &&
                object.definition.id != 4071 && object.definition.id != 8081) {
                move_one_tick(object, collision, result, terrain, contacts);
            }
        }
        if (!any_active)
            break;
    }
    return result;
}

std::size_t TemporaryObjects::active_count() const noexcept {
    return static_cast<std::size_t>(std::ranges::count(objects_, true, &TemporaryObject::active));
}

}  // namespace starhaven::game
