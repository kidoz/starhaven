#include "game/startup_flow.hpp"

namespace starhaven::game {

StartupFlow::StartupFlow(StartupState initial) noexcept : state_(initial) {}

bool StartupFlow::media_active() const noexcept {
    return state_ == StartupState::OpeningLogo || state_ == StartupState::OpeningIntro ||
           state_ == StartupState::Credits;
}

std::string_view StartupFlow::movie_name() const noexcept {
    switch (state_) {
    case StartupState::OpeningLogo:
        return "3dologo";
    case StartupState::OpeningIntro:
        return "MM6Intro";
    case StartupState::Credits:
        return "credits";
    default:
        return {};
    }
}

StartupTransition StartupFlow::dispatch(StartupAction action) noexcept {
    const StartupState from = state_;
    StartupEffect effect = StartupEffect::None;

    if (action == StartupAction::Quit) {
        state_ = StartupState::Quit;
        return {from, state_, StartupEffect::QuitApplication};
    }
    if (action == StartupAction::FatalFailure && state_ != StartupState::Quit) {
        state_ = StartupState::FatalError;
        return {from, state_, StartupEffect::None};
    }

    switch (state_) {
    case StartupState::Boot:
        if (action == StartupAction::ResourcesReady) {
            state_ = StartupState::OpeningLogo;
        } else if (action == StartupAction::ResourcesReadyWithoutMovies) {
            state_ = StartupState::Title;
        } else if (action == StartupAction::ResourcesMissing) {
            state_ = StartupState::InstallRequired;
        }
        break;
    case StartupState::InstallRequired:
        if (action == StartupAction::Retry) {
            state_ = StartupState::Boot;
        }
        break;
    case StartupState::OpeningLogo:
        if (action == StartupAction::MediaFinished || action == StartupAction::MediaSkipped ||
            action == StartupAction::MediaUnavailable) {
            state_ = StartupState::OpeningIntro;
        }
        break;
    case StartupState::OpeningIntro:
        if (action == StartupAction::MediaFinished || action == StartupAction::MediaSkipped ||
            action == StartupAction::MediaUnavailable) {
            state_ = StartupState::Title;
        }
        break;
    case StartupState::Title:
        if (action == StartupAction::ChooseNewGame) {
            state_ = StartupState::PartyCreation;
        } else if (action == StartupAction::ChooseLoad) {
            loading_return_ = StartupState::Title;
            state_ = StartupState::LoadingWorld;
            effect = StartupEffect::LoadCurrentSlot;
        } else if (action == StartupAction::ChooseCredits) {
            credits_seen_ = !credits_seen_;
            state_ = StartupState::Credits;
        } else if (action == StartupAction::ChooseExit || action == StartupAction::Back) {
            state_ = StartupState::Quit;
            effect = StartupEffect::QuitApplication;
        }
        break;
    case StartupState::PartyCreation:
        if (action == StartupAction::ConfirmParty) {
            state_ = StartupState::Playing;
        } else if (action == StartupAction::Back) {
            state_ = StartupState::Title;
        }
        break;
    case StartupState::SaveSelection:
        if (action == StartupAction::Back) {
            state_ = StartupState::Title;
        } else if (action == StartupAction::ChooseLoad) {
            loading_return_ = StartupState::SaveSelection;
            state_ = StartupState::LoadingWorld;
            effect = StartupEffect::LoadCurrentSlot;
        }
        break;
    case StartupState::Credits:
        if (action == StartupAction::MediaFinished || action == StartupAction::MediaSkipped ||
            action == StartupAction::MediaUnavailable || action == StartupAction::Back) {
            state_ = StartupState::Title;
        }
        break;
    case StartupState::LoadingWorld:
        if (action == StartupAction::LoadSucceeded) {
            state_ = StartupState::Playing;
        } else if (action == StartupAction::LoadFailed || action == StartupAction::Back) {
            state_ = loading_return_;
        }
        break;
    case StartupState::Playing:
        if (action == StartupAction::Back) {
            state_ = StartupState::Title;
        }
        break;
    case StartupState::FatalError:
        if (action == StartupAction::Back) {
            state_ = StartupState::Quit;
            effect = StartupEffect::QuitApplication;
        }
        break;
    case StartupState::Quit:
        break;
    }

    return {from, state_, effect};
}

}  // namespace starhaven::game
