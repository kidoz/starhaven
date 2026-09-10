#ifndef STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP
#define STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

#include "core/random.hpp"
#include "core/render/math3d.hpp"
#include "core/world/map_script.hpp"

namespace starhaven::world {
struct MapSession;
}  // namespace starhaven::world

namespace starhaven::game {

struct SavedDecoration {
    std::uint16_t descriptor = 0;
    bool visible = true;
    std::optional<std::uint8_t> event_value = std::nullopt;
    bool operator==(const SavedDecoration&) const = default;
};

// Original descriptor families with an implicit per-placement GLOBAL event.
[[nodiscard]] bool has_decoration_event(std::uint16_t descriptor) noexcept;
[[nodiscard]] std::uint8_t initial_decoration_event(std::uint16_t descriptor, Mm6Random& random);
void initialize_decoration_events(world::MapSession& session);

struct DecorationInteraction {
    std::uint32_t index = 0;
    std::uint16_t event = 0;
    bool global = false;
    float distance = 0;
};
[[nodiscard]] std::optional<DecorationInteraction>
decoration_interaction(const world::MapSession& session, std::uint32_t index);
[[nodiscard]] std::optional<DecorationInteraction>
aimed_decoration(const world::MapSession& session, render::Vec3 eye, render::Vec3 forward,
                 float range = 512.0f);

// Current results of executed changes, indexed by actual map filename and
// placed-decoration index. GLOBAL events still affect the currently loaded map.
using DecorationChanges = std::map<std::string, std::map<std::uint32_t, SavedDecoration>>;

[[nodiscard]] std::size_t apply_script_decorations(world::MapSession& session,
                                                   std::span<const world::DecorationChange> changes,
                                                   DecorationChanges& memory);
void restore_script_decorations(world::MapSession& session, const DecorationChanges& memory);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP
