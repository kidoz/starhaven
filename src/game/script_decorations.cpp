#include "game/script_decorations.hpp"

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

std::size_t apply_script_decorations(world::MapSession& session,
                                     std::span<const world::DecorationChange> changes,
                                     DecorationChanges& memory) {
    std::size_t applied = 0;
    for (const auto& change : changes) {
        if (change.index >= session.decorations.size()) {
            continue;
        }
        auto& decoration = session.decorations[change.index];
        if (change.name != "0") {
            const auto* type = session.decoration_types.find(change.name);
            const auto index =
                type == nullptr
                    ? std::size_t{0}
                    : static_cast<std::size_t>(type - session.decoration_types.entries().data());
            const auto descriptor = index > std::numeric_limits<std::uint16_t>::max()
                                        ? std::uint16_t{0}
                                        : static_cast<std::uint16_t>(index);
            // The original resolves unknown names to descriptor zero.
            set_descriptor(decoration, session.decoration_types, descriptor);
        }
        set_visible(decoration, change.visible);
        memory[script_scope(session.file_name)][change.index] = {
            decoration.descriptor_id,
            change.visible,
        };
        ++applied;
    }
    return applied;
}

void restore_script_decorations(world::MapSession& session, const DecorationChanges& memory) {
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
    }
}

}  // namespace starhaven::game
