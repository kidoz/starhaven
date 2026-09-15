#include "game/script_items.hpp"

#include <algorithm>
#include <limits>

#include "game/inventory.hpp"

namespace starhaven::game {

std::optional<data::GeneratedItem>
ScriptItemGenerator::generate(const world::ScriptItemRequest& request) {
    if (request.level < 1 || request.level > data::kTreasureLevelCount || request.type > 43 ||
        request.item_id > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        (request.item_id != 0 && items_.get().at(request.item_id) == nullptr)) {
        return std::nullopt;
    }
    Mm6Random random{state_.get().random};
    auto artifacts = state_.get().artifacts;
    data::GeneratedItem item;
    if (data::generate_random_item(random_items_, items_, standard_, special_, request.level,
                                   static_cast<data::ItemGenerationType>(request.type), random,
                                   artifacts, item) != data::ItemGenerationError::None) {
        return std::nullopt;
    }
    if (request.item_id != 0) {
        item.item_id = static_cast<int>(request.item_id);
    }
    state_.get().random = random.state();
    state_.get().artifacts = artifacts;
    return item;
}

void apply_script_items(std::span<const ScriptItemChange> changes, std::span<Pack> packs,
                        ScriptItemState& state, const data::ItemStatsTable& items) {
    for (const auto& change : changes) {
        if (!change.take) {
            if (change.item.item_id <= 0 ||
                items.at(static_cast<std::size_t>(change.item.item_id)) == nullptr) {
                continue;
            }
            state.pending.push_back(change.item);
            continue;
        }
        bool removed = false;
        for (auto& pack : packs) {
            const auto found =
                std::ranges::find(pack.items(), change.item.item_id, &PackedItem::item_id);
            if (found != pack.items().end()) {
                pack.remove(found->x, found->y);
                removed = true;
                break;
            }
        }
        if (!removed) {
            const auto found = std::ranges::find(state.pending, change.item.item_id,
                                                 &data::GeneratedItem::item_id);
            if (found != state.pending.end()) {
                state.pending.erase(found);
            }
        }
    }
}

bool claim_chest_items(int chest, std::span<const data::GeneratedItem> items,
                       std::set<int>& opened_chests, ScriptItemState& state) {
    if (chest < 0 || opened_chests.contains(chest)) {
        return false;
    }
    state.pending.insert(state.pending.end(), items.begin(), items.end());
    opened_chests.insert(chest);
    return true;
}

bool deliver_script_item(const data::GeneratedItem& item, int width, int height,
                         std::span<Pack> packs) {
    for (auto& pack : packs) {
        if (pack.add(item.item_id, width, height, item.identified, item.standard_bonus,
                     item.standard_bonus_strength, item.special_bonus, item.charges)) {
            return true;
        }
    }
    return false;
}

}  // namespace starhaven::game
