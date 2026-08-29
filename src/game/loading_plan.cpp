#include "game/loading_plan.hpp"

namespace starhaven::game {

void LoadingPlan::begin(const LoadRequestKind kind, const int slot) noexcept {
    kind_ = kind;
    slot_ = slot;
    phase_ = 0;
}

void LoadingPlan::advance() noexcept {
    if (phase_ < kPhaseCount) {
        ++phase_;
    }
}

float LoadingPlan::progress() const noexcept {
    return static_cast<float>(phase_) / static_cast<float>(kPhaseCount);
}

std::string_view LoadingPlan::phase_name(const std::size_t phase) noexcept {
    switch (phase) {
    case 0:
        return "Remembering the maps";
    case 1:
        return "Opening the world";
    case 2:
        return "Waking the party";
    default:
        return {};
    }
}

}  // namespace starhaven::game
