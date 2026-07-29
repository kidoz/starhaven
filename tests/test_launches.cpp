// Tests for sprites in flight: what a walked launch becomes and how it moves.
//
// Hermetic: the frame table is not needed — flight is geometry — except for
// resolving the group name, where a synthetic table stands in.
#include <catch2/catch_test_macros.hpp>

#include "game/launches.hpp"

using namespace starhaven;
using game::ActiveLaunch;

TEST_CASE("a launch flies toward its target and arrives", "[launches]") {
    std::vector<ActiveLaunch> flying;
    flying.push_back({"fire04", {0, 0, 0}, {game::kLaunchSpeed * 2, 0, 0}});

    game::advance_launches(flying, 1.0f);
    REQUIRE(flying.size() == 1);
    REQUIRE(flying[0].position.x > game::kLaunchSpeed * 0.99f);
    REQUIRE(flying[0].position.x < game::kLaunchSpeed * 1.01f);

    // The second second reaches the target, and the sprite is gone.
    game::advance_launches(flying, 1.0f);
    REQUIRE(flying.empty());
}

TEST_CASE("an aimless launch flies at the party", "[launches]") {
    world::MapLaunch record;
    record.animation = 999;  // no table resolves this
    record.from_x = 100;
    world::SpriteFrameTable empty;
    const auto none = game::start_launches({record}, empty, {0, 0, 0});
    // An animation the table does not hold launches nothing rather than a
    // blank billboard.
    REQUIRE(none.empty());
}
