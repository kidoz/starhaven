#ifndef STARHAVEN_GAME_STARTUP_FLOW_HPP
#define STARHAVEN_GAME_STARTUP_FLOW_HPP

#include <cstdint>
#include <string_view>

namespace starhaven::game {

// Application states from resource validation through a live world. The
// opening media and title order are observed MM6 behavior; installation and
// failure states are StarHaven's portable resource boundary.
enum class StartupState : std::uint8_t {
    Boot,
    InstallRequired,
    OpeningLogo,
    OpeningIntro,
    Title,
    PartyCreation,
    SaveSelection,
    Credits,
    LoadingWorld,
    Playing,
    FatalError,
    Quit,
};

enum class StartupAction : std::uint8_t {
    ResourcesReady,
    ResourcesReadyWithoutMovies,
    ResourcesMissing,
    Retry,
    MediaFinished,
    MediaSkipped,
    MediaUnavailable,
    MediaDecodeFailed,
    ChooseNewGame,
    ChooseLoad,
    ChooseCredits,
    ChooseExit,
    ConfirmParty,
    Back,
    LoadSucceeded,
    LoadFailed,
    FatalFailure,
    Quit,
};

// Effects are deliberately small. SDL and resource-owning code performs them,
// then reports completion through another action.
enum class StartupEffect : std::uint8_t {
    None,
    LoadCurrentSlot,
    LoadNewGame,
    QuitApplication,
};

struct StartupTransition {
    StartupState from = StartupState::Boot;
    StartupState to = StartupState::Boot;
    StartupEffect effect = StartupEffect::None;

    [[nodiscard]] bool changed() const noexcept { return from != to; }
};

class StartupFlow {
public:
    explicit StartupFlow(StartupState initial = StartupState::Boot) noexcept;

    [[nodiscard]] StartupState state() const noexcept { return state_; }
    [[nodiscard]] bool world_active() const noexcept { return state_ == StartupState::Playing; }
    [[nodiscard]] bool media_active() const noexcept;
    [[nodiscard]] bool credits_seen() const noexcept { return credits_seen_; }
    [[nodiscard]] std::string_view movie_name() const noexcept;

    [[nodiscard]] StartupTransition dispatch(StartupAction action) noexcept;

private:
    StartupState state_ = StartupState::Boot;
    StartupState loading_return_ = StartupState::Title;
    bool credits_seen_ = false;
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_STARTUP_FLOW_HPP
