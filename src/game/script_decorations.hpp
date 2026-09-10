#ifndef STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP
#define STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>

#include "core/world/map_script.hpp"

namespace starhaven::world {
struct MapSession;
}  // namespace starhaven::world

namespace starhaven::game {

struct SavedDecoration {
    std::uint16_t descriptor = 0;
    bool visible = true;
    bool operator==(const SavedDecoration&) const = default;
};

// Current results of executed changes, indexed by actual map filename and
// placed-decoration index. GLOBAL events still affect the currently loaded map.
using DecorationChanges = std::map<std::string, std::map<std::uint32_t, SavedDecoration>>;

[[nodiscard]] std::size_t apply_script_decorations(world::MapSession& session,
                                                   std::span<const world::DecorationChange> changes,
                                                   DecorationChanges& memory);
void restore_script_decorations(world::MapSession& session, const DecorationChanges& memory);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_DECORATIONS_HPP
