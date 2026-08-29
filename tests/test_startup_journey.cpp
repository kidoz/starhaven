#include "game/loading_plan.hpp"
#include "game/new_game.hpp"
#include "game/save_repository.hpp"
#include "game/startup_flow.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace starhaven::game;
using starhaven::Mm6Random;

namespace {

// A dealt party without the name table, as in the new-game tests.
std::array<Character, 4> dealt_party() {
    std::array<Character, 4> party;
    Mm6Random random{11};
    const std::array<std::string_view, 4> classes{"Sorcerer", "Knight", "Cleric", "Paladin"};
    for (std::size_t i = 0; i < party.size(); ++i) {
        Character& who = party[i];
        who.class_name = std::string(classes[i]);
        who.face = static_cast<int>(2 * i + 1) % kFaceCount;
        who.name = std::string("Journeyer ") + std::to_string(i + 1);
        roll_attributes(who, random);
        derive_start(who);
    }
    return party;
}

}  // namespace

TEST_CASE("the new-game journey reaches playing and returns home through a save", "[journey]") {
    StartupFlow flow;
    REQUIRE(flow.dispatch(StartupAction::ResourcesReadyWithoutMovies).to == StartupState::Title);
    REQUIRE(flow.dispatch(StartupAction::ChooseNewGame).to == StartupState::PartyCreation);

    // A confirmed draft leaves for the loading boundary carrying its seed,
    // and the boundary spends its named phases before playing.
    SaveState seed;
    REQUIRE(make_new_game_state(dealt_party(), true, seed));
    REQUIRE(flow.dispatch(StartupAction::ConfirmParty).effect == StartupEffect::LoadNewGame);
    REQUIRE(flow.state() == StartupState::LoadingWorld);
    REQUIRE_FALSE(flow.world_active());

    LoadingPlan plan;
    plan.begin(LoadRequestKind::NewGame, 0);
    while (plan.active()) {
        REQUIRE_FALSE(LoadingPlan::phase_name(plan.phase()).empty());
        plan.advance();
    }

    REQUIRE(flow.dispatch(StartupAction::LoadSucceeded).to == StartupState::Playing);
    REQUIRE(flow.world_active());

    // The seed is a save like any other: a repository holding it reads the
    // same journey back, and loading it plays it.
    const auto dir = std::filesystem::temp_directory_path() / "starhaven-journey-test";
    std::filesystem::create_directories(dir);
    {
        const SaveRepository saves{dir, {{"OutE3.Odm", "New Sorpigal"}}};
        {
            std::ofstream file(saves.path_for_slot(1), std::ios::binary);
            file << save_text(seed);
        }
        REQUIRE(saves.inspect(1).loadable());

        REQUIRE(flow.dispatch(StartupAction::Back).to == StartupState::Title);
        REQUIRE(flow.dispatch(StartupAction::ChooseLoad).to == StartupState::SaveSelection);

        SaveState loaded;
        const auto info = saves.load(1, loaded);
        REQUIRE(info.loadable());
        REQUIRE(info.map_name == "New Sorpigal");
        REQUIRE(loaded.map_file == seed.map_file);
        REQUIRE(loaded.gold == seed.gold);
        REQUIRE(loaded.bits == seed.bits);
        REQUIRE(loaded.party[0].name == seed.party[0].name);

        REQUIRE(flow.dispatch(StartupAction::ChooseLoad).effect == StartupEffect::LoadCurrentSlot);
        REQUIRE(flow.state() == StartupState::LoadingWorld);
        REQUIRE(flow.dispatch(StartupAction::LoadSucceeded).to == StartupState::Playing);
        REQUIRE(flow.world_active());
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("an empty slot holds the journey at save selection", "[journey]") {
    const auto dir = std::filesystem::temp_directory_path() / "starhaven-journey-empty";
    std::filesystem::create_directories(dir);
    const SaveRepository saves{dir, {{"OutE3.Odm", "New Sorpigal"}}};

    StartupFlow flow{StartupState::Title};
    REQUIRE(flow.dispatch(StartupAction::ChooseLoad).to == StartupState::SaveSelection);
    REQUIRE(flow.dispatch(StartupAction::ChooseLoad).effect == StartupEffect::LoadCurrentSlot);
    REQUIRE(flow.state() == StartupState::LoadingWorld);

    // Nothing was published, so the failure only returns to the menu.
    REQUIRE(flow.dispatch(StartupAction::LoadFailed).to == StartupState::SaveSelection);
    REQUIRE(flow.dispatch(StartupAction::Back).to == StartupState::Title);
    std::filesystem::remove_all(dir);
}
