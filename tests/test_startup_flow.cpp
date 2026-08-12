#include "game/startup_flow.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace starhaven::game;

TEST_CASE("the normal opening advances through both movies to the title", "[startup]") {
    StartupFlow flow;

    REQUIRE(flow.state() == StartupState::Boot);
    REQUIRE(flow.dispatch(StartupAction::ResourcesReady).to == StartupState::OpeningLogo);
    REQUIRE(flow.media_active());
    REQUIRE(flow.movie_name() == "3dologo");

    REQUIRE(flow.dispatch(StartupAction::MediaFinished).to == StartupState::OpeningIntro);
    REQUIRE(flow.movie_name() == "MM6Intro");

    REQUIRE(flow.dispatch(StartupAction::MediaSkipped).to == StartupState::Title);
    REQUIRE_FALSE(flow.media_active());
    REQUIRE(flow.movie_name().empty());
}

TEST_CASE("opening movies can be bypassed without entering the world", "[startup]") {
    StartupFlow flow;

    const auto transition = flow.dispatch(StartupAction::ResourcesReadyWithoutMovies);

    REQUIRE(transition.to == StartupState::Title);
    REQUIRE_FALSE(flow.world_active());
}

TEST_CASE("new game can be cancelled or confirmed from party creation", "[startup]") {
    StartupFlow flow{StartupState::Title};

    REQUIRE(flow.dispatch(StartupAction::ChooseNewGame).to == StartupState::PartyCreation);
    REQUIRE(flow.dispatch(StartupAction::Back).to == StartupState::Title);
    REQUIRE(flow.dispatch(StartupAction::ChooseNewGame).to == StartupState::PartyCreation);
    REQUIRE(flow.dispatch(StartupAction::ConfirmParty).to == StartupState::Playing);
    REQUIRE(flow.world_active());
}

TEST_CASE("credits return to the title after completion or skip", "[startup]") {
    StartupFlow flow{StartupState::Title};

    REQUIRE(flow.dispatch(StartupAction::ChooseCredits).to == StartupState::Credits);
    REQUIRE(flow.movie_name() == "credits");
    REQUIRE(flow.credits_seen());
    REQUIRE(flow.dispatch(StartupAction::MediaFinished).to == StartupState::Title);

    REQUIRE(flow.dispatch(StartupAction::ChooseCredits).to == StartupState::Credits);
    REQUIRE_FALSE(flow.credits_seen());
    REQUIRE(flow.dispatch(StartupAction::MediaSkipped).to == StartupState::Title);
}

TEST_CASE("load failure returns to its menu and success alone enters play", "[startup]") {
    StartupFlow flow{StartupState::Title};

    auto transition = flow.dispatch(StartupAction::ChooseLoad);
    REQUIRE(transition.to == StartupState::LoadingWorld);
    REQUIRE(transition.effect == StartupEffect::LoadCurrentSlot);
    REQUIRE_FALSE(flow.world_active());

    transition = flow.dispatch(StartupAction::ConfirmParty);
    REQUIRE_FALSE(transition.changed());
    REQUIRE(flow.state() == StartupState::LoadingWorld);

    REQUIRE(flow.dispatch(StartupAction::LoadFailed).to == StartupState::Title);
    REQUIRE(flow.dispatch(StartupAction::ChooseLoad).to == StartupState::LoadingWorld);
    REQUIRE(flow.dispatch(StartupAction::LoadSucceeded).to == StartupState::Playing);
    REQUIRE(flow.world_active());
}

TEST_CASE("missing media advances but unrelated actions do not", "[startup]") {
    StartupFlow flow{StartupState::OpeningLogo};

    const auto ignored = flow.dispatch(StartupAction::LoadSucceeded);
    REQUIRE_FALSE(ignored.changed());
    REQUIRE(flow.state() == StartupState::OpeningLogo);

    REQUIRE(flow.dispatch(StartupAction::MediaUnavailable).to == StartupState::OpeningIntro);
    REQUIRE(flow.dispatch(StartupAction::MediaUnavailable).to == StartupState::Title);
}

TEST_CASE("back behavior is explicit in each menu phase", "[startup]") {
    StartupFlow install{StartupState::InstallRequired};
    REQUIRE_FALSE(install.dispatch(StartupAction::Back).changed());
    REQUIRE(install.dispatch(StartupAction::Retry).to == StartupState::Boot);

    StartupFlow opening{StartupState::OpeningIntro};
    REQUIRE_FALSE(opening.dispatch(StartupAction::Back).changed());

    StartupFlow title{StartupState::Title};
    auto transition = title.dispatch(StartupAction::Back);
    REQUIRE(transition.to == StartupState::Quit);
    REQUIRE(transition.effect == StartupEffect::QuitApplication);

    StartupFlow selection{StartupState::SaveSelection};
    REQUIRE(selection.dispatch(StartupAction::Back).to == StartupState::Title);

    StartupFlow loading{StartupState::LoadingWorld};
    REQUIRE(loading.dispatch(StartupAction::Back).to == StartupState::Title);

    StartupFlow playing{StartupState::Playing};
    REQUIRE(playing.dispatch(StartupAction::Back).to == StartupState::Title);
}

TEST_CASE("quit has one terminal state from every active phase", "[startup]") {
    for (const StartupState state :
         {StartupState::Boot, StartupState::InstallRequired, StartupState::OpeningLogo,
          StartupState::OpeningIntro, StartupState::Title, StartupState::PartyCreation,
          StartupState::SaveSelection, StartupState::Credits, StartupState::LoadingWorld,
          StartupState::Playing, StartupState::FatalError}) {
        StartupFlow flow{state};
        const auto transition = flow.dispatch(StartupAction::Quit);
        REQUIRE(transition.to == StartupState::Quit);
        REQUIRE(transition.effect == StartupEffect::QuitApplication);
    }
}
