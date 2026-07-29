#ifndef STARHAVEN_GAME_TEMPLE_HPP
#define STARHAVEN_GAME_TEMPLE_HPP

// Healing at a temple.
//
// The ten `Temple` rows of `2DEvents.txt` write their own terms: the margin
// notes name the two actions — `"Heal (cost)"` and `"Donate (cost)"` — the
// `Val` column scales the price, and the first stock cell is the service
// ceiling: `"All OK"`, `"No Errad"`, or Temple Baa's `"No Dead,Stone,
// Errad"`. `observed` What a heal costs in gold and what a donation earns
// are this engine's, and say so.

#include <string_view>

#include "core/data/building_stats.hpp"

namespace starhaven::game {

// What a heal costs: the row's own `Val`, in gold. `inferred`
[[nodiscard]] inline int heal_price(const data::BuildingStatsEntry& shop) noexcept {
    const auto price = static_cast<int>(shop.price_factor);
    return price < 1 ? 1 : price;
}

[[nodiscard]] inline bool is_temple(const data::BuildingStatsEntry& shop) noexcept {
    return shop.type == "Temple";
}

// The service ceiling the row's own cell writes. The conditions it names —
// dead, stone, eradicated — are ahead of what this engine tracks, so today
// the ceiling is recorded and shown rather than enforced against them.
struct TempleService {
    bool heals_dead = true;
    bool heals_stone = true;
    bool heals_eradicated = true;
};

[[nodiscard]] inline TempleService temple_service(const data::BuildingStatsEntry& shop) noexcept {
    TempleService out;
    const std::string_view cell = shop.stock_a;
    if (cell.find("All OK") != std::string_view::npos) {
        return out;
    }
    if (cell.find("Dead") != std::string_view::npos) {
        out.heals_dead = false;
    }
    if (cell.find("Stone") != std::string_view::npos) {
        out.heals_stone = false;
    }
    if (cell.find("Errad") != std::string_view::npos) {
        out.heals_eradicated = false;
    }
    return out;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TEMPLE_HPP
