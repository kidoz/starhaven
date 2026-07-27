// Tests for the time of day: how light it is, where the sun is, and what
// colour the sky is.
//
// Hermetic: nothing but the clock.
#include <catch2/catch_test_macros.hpp>

#include "game/daylight.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

GameClock at(int hour, int minute = 0) {
    return GameClock{static_cast<std::int64_t>(hour) * kMinutesPerHour + minute};
}

}  // namespace

TEST_CASE("it is darkest in the small hours and brightest at midday", "[daylight]") {
    REQUIRE(daylight_amount(at(2)) == 0.0f);
    REQUIRE(daylight_amount(at(23)) == 0.0f);
    REQUIRE(daylight_amount(at(13)) > daylight_amount(at(8)));
    REQUIRE(daylight_amount(at(13)) > daylight_amount(at(19)));
    REQUIRE(daylight_amount(at(13)) <= 1.0f);
}

TEST_CASE("dawn and dusk are gradual", "[daylight]") {
    // Not a switch thrown on the hour: the minutes either side differ.
    REQUIRE(daylight_amount(at(5, 30)) > daylight_amount(at(5, 0)));
    REQUIRE(daylight_amount(at(6, 30)) > daylight_amount(at(5, 30)));
    REQUIRE(daylight_amount(at(20, 30)) < daylight_amount(at(20, 0)));
}

TEST_CASE("the world never goes completely black", "[daylight]") {
    // A party that cannot see at all cannot play.
    for (int hour = 0; hour < kHoursPerDay; ++hour) {
        REQUIRE(light_level(at(hour)) > 0.2f);
        REQUIRE(light_level(at(hour)) <= 1.0f);
    }
}

TEST_CASE("the sun crosses the sky and never sinks through the floor", "[daylight]") {
    const render::Vec3 morning = sun_direction(at(8));
    const render::Vec3 noon = sun_direction(at(13));
    const render::Vec3 evening = sun_direction(at(18));

    REQUIRE(noon.y > morning.y);
    REQUIRE(noon.y > evening.y);
    REQUIRE(morning.x > evening.x);  // east to west
    for (int hour = 0; hour < kHoursPerDay; ++hour) {
        REQUIRE(sun_direction(at(hour)).y > 0.0f);
    }
}

TEST_CASE("the sky is dark at night and blue by day", "[daylight]") {
    const render::Color night = sky_colour(at(1));
    const render::Color day = sky_colour(at(13));
    REQUIRE(day.b > night.b);
    REQUIRE(day.r > night.r);
    // Blue is the strongest channel in a daylit sky.
    REQUIRE(day.b > day.g);
    REQUIRE(day.g > day.r);
}

TEST_CASE("dawn and dusk are warmer than the hour on either side", "[daylight]") {
    const render::Color dawn = sky_colour(at(6));
    const render::Color mid_morning = sky_colour(at(10));
    REQUIRE(dawn.r > dawn.b);
    REQUIRE(mid_morning.b > mid_morning.r);
}
