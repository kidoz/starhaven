#ifndef STARHAVEN_GAME_SCRIPT_ITEMS_HPP
#define STARHAVEN_GAME_SCRIPT_ITEMS_HPP

#include <functional>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "core/data/item_generation.hpp"
#include "core/world/map_script.hpp"

namespace starhaven::game {

// Persistent rewards from script grants and claimed chests share this queue.
struct ScriptItemState {
    std::uint32_t random = 0x41A73B29U;
    data::ArtifactGenerationState artifacts;
    std::vector<data::GeneratedItem> pending;
};

// Table-driven generation is synchronous: later checks in this event see the
// generated item. Invalid requests leave the generator state unchanged.
class ScriptItemGenerator {
public:
    ScriptItemGenerator(const data::RandomItemTable& random_items,
                        const data::ItemStatsTable& items, const data::StandardBonusTable& standard,
                        const data::SpecialBonusTable& special, ScriptItemState& state)
        : random_items_(random_items), items_(items), standard_(standard), special_(special),
          state_(state) {}

    [[nodiscard]] std::optional<data::GeneratedItem>
    generate(const world::ScriptItemRequest& request);

private:
    std::reference_wrapper<const data::RandomItemTable> random_items_;
    std::reference_wrapper<const data::ItemStatsTable> items_;
    std::reference_wrapper<const data::StandardBonusTable> standard_;
    std::reference_wrapper<const data::SpecialBonusTable> special_;
    std::reference_wrapper<ScriptItemState> state_;
};

class Pack;

struct ScriptItemChange {
    data::GeneratedItem item;
    bool take = false;
};

// Apply in script order. New grants wait until the whole event has finished,
// so a later take consumes the same instance that the walker checked.
void apply_script_items(std::span<const ScriptItemChange> changes, std::span<Pack> packs,
                        ScriptItemState& state, const data::ItemStatsTable& items);

// Claim once, retaining every generated instance before marking the chest
// opened. Pack-space retries use the same persistent queue as script grants.
[[nodiscard]] bool claim_chest_items(int chest, std::span<const data::GeneratedItem> items,
                                     std::set<int>& opened_chests, ScriptItemState& state);

// Try each pack without changing the item. False retains a full-pack reward
// in the caller's persistent pending list.
[[nodiscard]] bool deliver_script_item(const data::GeneratedItem& item, int width, int height,
                                       std::span<Pack> packs);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_ITEMS_HPP
