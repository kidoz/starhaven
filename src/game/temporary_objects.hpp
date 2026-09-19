#ifndef STARHAVEN_GAME_TEMPORARY_OBJECTS_HPP
#define STARHAVEN_GAME_TEMPORARY_OBJECTS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "core/random.hpp"
#include "core/render/color.hpp"
#include "core/world/collision.hpp"
#include "core/world/map_script.hpp"
#include "core/world/object_table.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace starhaven::game {

inline constexpr std::size_t kTemporaryObjectCapacity = 1000;
inline constexpr std::uint32_t kObjectTicksPerSecond = 128;
inline constexpr std::size_t kObjectTrailCapacity = 100;
// Engine policy: original particle emission/jitter was frame-coupled.
inline constexpr std::uint32_t kObjectTrailStepTicks = 4;

struct ObjectTrailParticle {
    render::Vec3 position;
    render::Color color;
    std::uint32_t remaining = 0;  // 128 Hz ticks; independent of the emitting object
};

struct TemporaryObjectDefinition {
    std::uint16_t id = 0;
    std::uint16_t descriptor = 0;
    std::uint16_t frame = 0;
    std::uint16_t flags = 0;
    std::uint32_t lifetime = 0;
    float radius = 0;
    render::Color trail_color;
};

struct TemporaryObject {
    TemporaryObjectDefinition definition;
    std::optional<TemporaryObjectDefinition> impact_definition;
    render::Vec3 position;  // renderer axes, base of object, Y up
    render::Vec3 origin;
    render::Vec3 previous;
    render::Vec3 velocity;  // map units per second
    std::uint32_t age = 0;  // simulation ticks, never wall-clock time
    bool active = true;
    bool resting = false;
    // Engine overlap policy: each 1000/2081/2100 reacts once until it leaves this body.
    std::optional<std::size_t> touching_actor = std::nullopt;
    bool touching_party = false;  // IDs 1000/2081 use the same separation policy
};

enum class TemporarySpawnError : std::uint8_t { None, UnsupportedId, MissingDescriptor, BadFrame };
struct TemporarySpawnResult {
    TemporarySpawnError error = TemporarySpawnError::None;
    std::size_t created = 0;
    std::size_t dropped = 0;  // attempts that found the pool full
};

struct ObjectDetonation {
    render::Vec3 position;
    float radius = 512;
    // Opcode 34 supplies source zero. These unowned effects do not deliver
    // party or actor damage through the original area-effect consumers.
};
// Candidate bodies are supplied by the live combat adapter. Expanded cylinders
// are a StarHaven collision policy, not a recreation of MM6's sector solver.
struct ObjectActor {
    std::size_t index = 0;
    render::Vec3 position;
    float radius = 0;
    float height = 0;
};
struct ObjectParty {
    render::Vec3 position;  // feet, renderer axes
    float radius = 0;
    float height = 0;
};
struct ObjectContacts {
    std::span<const ObjectActor> bodies;
    std::function<bool(std::size_t)> apply;  // ID 8080 resistance gate and state response
    std::optional<ObjectParty> party = std::nullopt;
    // Ordinary collision response for IDs 1000, 2081 and 2100.
    std::function<void(std::size_t)> react_to_actor = nullptr;
};
struct TemporaryObjectStep {
    std::size_t expired = 0;
    std::size_t bounces = 0;
    std::size_t terrain_contacts = 0;
    std::size_t actor_contacts = 0;
    std::size_t party_contacts = 0;
    std::size_t actor_accepted = 0;  // accepted ID 8080 resistance gates
    std::size_t missing_actor_replacements = 0;
    std::size_t actor_redirects = 0;
    std::size_t actor_animation_fallbacks = 0;
    std::size_t trail_emitted = 0;
    std::vector<ObjectDetonation> detonations;
};

// Shared trajectory and geometry response for persistent event loot. The
// caller owns lifetime and pickup; this only advances one 128 Hz motion tick.
[[nodiscard]] render::Vec3 object_launch_velocity(std::int32_t speed, std::uint16_t yaw,
                                                  std::uint16_t pitch);
void advance_object_motion(TemporaryObject& object, const world::CollisionWorld& collision,
                           const world::OdmTerrain* terrain = nullptr);

// Bounded lifecycle for event-created IDs 1000/1050/2081/2100/4070/8080 only. Not a
// general opcode-34 implementation: loot, other families' trails, sound and
// persistence remain outside this system. Every accepted definition must be
// temporary. StarHaven uses one-tick integration and swept spheres; it does
// not reproduce the original sector solver or integer trajectory rounding.
class TemporaryObjects {
public:
    [[nodiscard]] TemporarySpawnResult spawn(const world::ObjectSpawnRequest& request,
                                             std::span<const world::ObjectDescriptor> objects,
                                             std::span<const world::SpriteFrame> frames,
                                             Mm6Random& random, std::size_t occupied = 0);
    [[nodiscard]] TemporaryObjectStep advance(std::uint32_t ticks,
                                              const world::CollisionWorld& collision,
                                              const world::OdmTerrain* terrain = nullptr,
                                              const ObjectContacts* contacts = nullptr);
    [[nodiscard]] std::span<const TemporaryObject> slots() const noexcept { return objects_; }
    [[nodiscard]] std::span<const ObjectTrailParticle> trail_particles() const noexcept {
        return particles_;
    }
    [[nodiscard]] std::size_t active_count() const noexcept;
    void clear() noexcept;

private:
    void emit_trail_particle(render::Vec3 position, render::Color color);
    [[nodiscard]] std::size_t emit_trail_burst(render::Vec3 position, render::Color color);
    std::vector<TemporaryObject> objects_;
    std::array<ObjectTrailParticle, kObjectTrailCapacity> particles_{};
    std::size_t next_particle_ = 0;
    std::uint32_t trail_tick_ = 0;
    Mm6Random trail_random_{1};  // visual-only sequence; never consumes gameplay RNG
};

}  // namespace starhaven::game
#endif  // STARHAVEN_GAME_TEMPORARY_OBJECTS_HPP
