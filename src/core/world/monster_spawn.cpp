#include "core/world/monster_spawn.hpp"

#include <cmath>
#include <cstddef>
#include <string>

#include "core/data/text_table.hpp"

namespace starhaven::world {

SpawnCount parse_spawn_count(std::string_view text) noexcept {
    const std::string_view trimmed = data::trim(text);
    if (trimmed.empty()) {
        return {};
    }
    const std::size_t dash = trimmed.find('-');
    if (dash == std::string_view::npos) {
        const int one = data::parse_int(trimmed, 0);
        return one > 0 ? SpawnCount{one, one} : SpawnCount{};
    }
    const int low = data::parse_int(data::trim(trimmed.substr(0, dash)), 0);
    const int high = data::parse_int(data::trim(trimmed.substr(dash + 1)), 0);
    if (low <= 0 || high < low) {
        return {};
    }
    return {low, high};
}

int encounter_slot_of(std::uint32_t spawn_index) noexcept {
    if (spawn_index == 0) {
        return -1;
    }
    return static_cast<int>((spawn_index - 1) % 3);
}

int encounter_monster_id(const data::MonsterStatsTable& monsters,
                         const data::MapEncounter& slot) noexcept {
    if (slot.empty()) {
        return 0;
    }
    const data::MonsterStatsEntry* row = monsters.find(slot.picture + "A");
    if (row == nullptr) {
        row = monsters.find(slot.picture);
    }
    if (row == nullptr) {
        row = monsters.find("z" + slot.picture);
    }
    return row == nullptr ? 0 : row->id;
}

std::pair<float, float> spawn_offset(int n, int group_size) noexcept {
    if (group_size <= 1) {
        return {0.0f, 0.0f};
    }
    // Around the point, not on it: a group stacked on one spot reads as one
    // monster from every angle.
    constexpr float kTwoPi = 6.2831853f;
    const float angle = kTwoPi * static_cast<float>(n) / static_cast<float>(group_size);
    return {std::cos(angle) * kSpawnGroupSpread, std::sin(angle) * kSpawnGroupSpread};
}

}  // namespace starhaven::world
