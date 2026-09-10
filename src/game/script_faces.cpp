#include "game/script_faces.hpp"

#include <limits>

#include "core/world/map_session.hpp"
#include "game/script_walk.hpp"

namespace starhaven::game {

std::size_t apply_script_faces(world::MapSession& session,
                               std::span<const world::FaceChange> changes, FaceChanges& memory) {
    if (!session.indoor())
        return 0;
    std::size_t applied = 0;
    bool collision_changed = false;
    for (const auto& change : changes) {
        if (change.index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
            change.index >= session.blv.faces.size())
            continue;
        auto& face = session.blv.faces[change.index];
        const auto before = face.attributes;
        if (change.texture.empty()) {
            face.attributes =
                change.set ? face.attributes | change.mask : face.attributes & ~change.mask;
        } else {
            face.texture_name = change.texture;
            if ((face.attributes & world::kFaceTextureAnimated) != 0 &&
                world::find_texture_animation(session.texture_animations, change.texture) ==
                    nullptr)
                face.attributes &= ~world::kFaceTextureAnimated;
        }
        collision_changed |= before != face.attributes;
        memory[script_scope(session.file_name)][change.index] = {
            face.attributes,
            face.texture_name,
        };
        ++applied;
    }
    if (collision_changed)
        world::rebuild_indoor_collision(session);
    return applied;
}

void restore_script_faces(world::MapSession& session, const FaceChanges& memory) {
    if (!session.indoor())
        return;
    const auto found = memory.find(script_scope(session.file_name));
    if (found == memory.end())
        return;
    for (const auto& [index, state] : found->second) {
        if (index >= session.blv.faces.size())
            continue;
        auto& face = session.blv.faces[index];
        face.attributes = state.attributes;
        face.texture_name = state.texture;
    }
    world::rebuild_indoor_collision(session);
}

}  // namespace starhaven::game
