#include "game/loading_plan.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace starhaven::game;

TEST_CASE("a spent plan is inactive and shows nothing", "[loading]") {
    LoadingPlan plan;

    REQUIRE_FALSE(plan.active());
    REQUIRE(plan.progress() == 1.0f);
    REQUIRE(LoadingPlan::phase_name(LoadingPlan::kPhaseCount).empty());
}

TEST_CASE("a request walks the named phases in order and is spent at the end", "[loading]") {
    LoadingPlan plan;
    plan.begin(LoadRequestKind::SavedSlot, 4);

    REQUIRE(plan.active());
    REQUIRE(plan.kind() == LoadRequestKind::SavedSlot);
    REQUIRE(plan.slot() == 4);
    REQUIRE(plan.phase() == 0);
    REQUIRE(plan.progress() == 0.0f);

    float last = plan.progress();
    for (std::size_t i = 0; i < LoadingPlan::kPhaseCount; ++i) {
        REQUIRE_FALSE(LoadingPlan::phase_name(plan.phase()).empty());

        plan.advance();
        REQUIRE(plan.progress() >= last);
        last = plan.progress();
    }

    REQUIRE_FALSE(plan.active());
    REQUIRE(plan.progress() == 1.0f);
    // Advancing a spent plan goes nowhere.
    plan.advance();
    REQUIRE_FALSE(plan.active());
}

TEST_CASE("the three phases carry their names in order", "[loading]") {
    REQUIRE(LoadingPlan::phase_name(0) == "Remembering the maps");
    REQUIRE(LoadingPlan::phase_name(1) == "Opening the world");
    REQUIRE(LoadingPlan::phase_name(2) == "Waking the party");
}

TEST_CASE("a new request begins the phases afresh", "[loading]") {
    LoadingPlan plan;
    plan.begin(LoadRequestKind::SavedSlot, 9);
    plan.advance();
    plan.advance();

    plan.begin(LoadRequestKind::NewGame, 0);
    REQUIRE(plan.active());
    REQUIRE(plan.kind() == LoadRequestKind::NewGame);
    REQUIRE(plan.phase() == 0);
    REQUIRE(plan.progress() == 0.0f);
}
