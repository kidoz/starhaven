#ifndef STARHAVEN_GAME_SCRIPT_FACES_HPP
#define STARHAVEN_GAME_SCRIPT_FACES_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>

#include "core/world/map_script.hpp"

namespace starhaven::world {
struct MapSession;
}

namespace starhaven::game {

struct SavedFace {
    std::uint32_t attributes = 0;
    std::string texture;
    bool operator==(const SavedFace&) const = default;
};
using FaceChanges = std::map<std::string, std::map<std::uint32_t, SavedFace>>;

// Apply indoor-only changes in order and rebuild collision once when needed.
[[nodiscard]] std::size_t apply_script_faces(world::MapSession& session,
                                             std::span<const world::FaceChange> changes,
                                             FaceChanges& memory);
void restore_script_faces(world::MapSession& session, const FaceChanges& memory);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_FACES_HPP
