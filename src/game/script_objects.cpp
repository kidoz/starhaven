#include "game/script_objects.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace starhaven::game {

std::optional<ObjectSpawnResources>
resolve_object_spawn(const world::ObjectSpawnRequest& request,
                     std::span<const world::ObjectDescriptor> objects,
                     std::span<const data::ItemStatsEntry> items) {
    const auto object_id = static_cast<std::uint16_t>(request.object_id);
    const auto descriptor =
        std::ranges::find(objects, object_id, &world::ObjectDescriptor::object_id);
    if (descriptor == objects.end())
        return std::nullopt;
    const auto index = descriptor - objects.begin();
    if (!std::in_range<std::uint16_t>(index))
        return std::nullopt;
    ObjectSpawnResources result;
    result.descriptor_index = static_cast<std::uint16_t>(index);
    // The executable compares the full u32 request to a zero-extended byte
    // from compiled ITEMS. Truncating the request here would manufacture loot.
    if (request.object_id <= std::numeric_limits<std::uint8_t>::max()) {
        const auto item = std::ranges::find_if(items, [&](const data::ItemStatsEntry& entry) {
            return static_cast<std::uint8_t>(entry.sprite_index) == request.object_id;
        });
        if (item != items.end())
            result.item_id = item->id;
    }
    return result;
}

}  // namespace starhaven::game
