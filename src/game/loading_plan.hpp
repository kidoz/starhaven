#ifndef STARHAVEN_GAME_LOADING_PLAN_HPP
#define STARHAVEN_GAME_LOADING_PLAN_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace starhaven::game {

// What the loading boundary has been asked to bring into the world.
enum class LoadRequestKind : std::uint8_t {
    NewGame,
    SavedSlot,
};

// The named steps between a confirmed request and a published session.
// The adapter runs one phase per frame, so the window stays responsive to
// Quit and the progress rectangle moves by the steps' own count rather
// than by frames.
class LoadingPlan {
public:
    // The same three steps for either kind: what the maps away from the
    // party remember, the map session itself, then the party's own state.
    static constexpr std::size_t kPhaseCount = 3;

    void begin(LoadRequestKind kind, int slot) noexcept;

    // One named step done; the plan is spent after the last.
    void advance() noexcept;

    [[nodiscard]] bool active() const noexcept { return phase_ < kPhaseCount; }
    [[nodiscard]] std::size_t phase() const noexcept { return phase_; }
    [[nodiscard]] LoadRequestKind kind() const noexcept { return kind_; }
    [[nodiscard]] int slot() const noexcept { return slot_; }

    // How much of the boundary's work sits behind it, 0 to 1.
    [[nodiscard]] float progress() const noexcept;

    // The name the loading screen shows for a step; empty past the end.
    [[nodiscard]] static std::string_view phase_name(std::size_t phase) noexcept;

private:
    LoadRequestKind kind_ = LoadRequestKind::NewGame;
    int slot_ = 0;
    std::size_t phase_ = kPhaseCount;  // spent until a request begins
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_LOADING_PLAN_HPP
