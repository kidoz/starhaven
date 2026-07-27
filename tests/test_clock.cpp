// Tests for game time and for resting.
//
// Hermetic: no archives, no tables.
#include <catch2/catch_test_macros.hpp>

#include "game/clock.hpp"
#include "game/rest.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

Character fighter(int hp, int sp = 0) {
    Character c;
    c.name = "Aaron";
    c.hit_points = hp;
    c.max_hit_points = 30;
    c.spell_points = sp;
    c.max_spell_points = 10;
    return c;
}

}  // namespace

TEST_CASE("the clock counts hours, days and weekdays", "[clock]") {
    GameClock clock;  // day one, morning
    REQUIRE(clock.day() == 0);
    REQUIRE(clock.hour() == 9);
    REQUIRE(clock.minute() == 0);
    REQUIRE(clock.weekday() == "Sunday");

    clock.advance_hours(kHoursPerDay);
    REQUIRE(clock.day() == 1);
    REQUIRE(clock.hour() == 9);
    REQUIRE(clock.weekday() == "Monday");
}

TEST_CASE("the week is the seven PROFTEXT names, in order, forever", "[clock]") {
    GameClock clock{0};
    for (int day = 0; day < 30; ++day) {
        REQUIRE(clock.weekday() == kWeekdays[static_cast<std::size_t>(day % 7)]);
        clock.advance_hours(kHoursPerDay);
    }
}

TEST_CASE("seconds accumulate into whole minutes", "[clock]") {
    // A fraction of a minute must not be lost each frame, or the clock runs
    // slow by however often the engine happens to tick.
    GameClock clock{0};
    for (int frame = 0; frame < 60 * 60; ++frame) {
        clock.advance_seconds(1.0f / 60.0f);
    }
    REQUIRE(clock.minutes() == 60);
}

TEST_CASE("an establishment's hours are read the way the table writes them", "[clock]") {
    // 2DEvents.txt gives an opening and a closing hour, and some places close
    // after midnight.
    const GameClock noon{12 * kMinutesPerHour};
    REQUIRE(noon.open(9, 17));
    REQUIRE_FALSE(noon.open(18, 23));

    const GameClock small_hours{2 * kMinutesPerHour};
    REQUIRE(small_hours.open(20, 4));
    REQUIRE_FALSE(small_hours.open(9, 17));
    // A place that opens and closes at the same hour is never open.
    REQUIRE_FALSE(noon.open(12, 12));
}

TEST_CASE("resting heals everyone standing and moves the clock", "[clock]") {
    std::array<Character, 4> party{fighter(5), fighter(1, 2), fighter(30, 10), fighter(9)};
    GameClock clock;
    const std::int64_t before = clock.minutes();

    REQUIRE(rest(party, clock, false) == RestResult::Rested);
    REQUIRE(clock.minutes() == before + kRestHours * kMinutesPerHour);
    for (const auto& who : party) {
        REQUIRE(who.hit_points == who.max_hit_points);
        REQUIRE(who.spell_points == who.max_spell_points);
    }
}

TEST_CASE("a night's sleep does not stand the fallen back up", "[clock]") {
    // Being down is not the same as being hurt.
    std::array<Character, 4> party{fighter(0), fighter(10), fighter(10), fighter(10)};
    GameClock clock;
    REQUIRE(rest(party, clock, false) == RestResult::Rested);
    REQUIRE(party[0].hit_points == 0);
    REQUIRE(party[1].hit_points == party[1].max_hit_points);
}

TEST_CASE("monsters nearby stop a rest, and stop the clock too", "[clock]") {
    std::array<Character, 4> party{fighter(5), fighter(5), fighter(5), fighter(5)};
    GameClock clock;
    const std::int64_t before = clock.minutes();

    REQUIRE(rest(party, clock, true) == RestResult::Disturbed);
    REQUIRE(clock.minutes() == before);
    REQUIRE(party[0].hit_points == 5);
}

TEST_CASE("a party with nobody standing cannot make camp", "[clock]") {
    std::array<Character, 4> party{fighter(0), fighter(0), fighter(0), fighter(0)};
    GameClock clock;
    REQUIRE(rest(party, clock, false) == RestResult::NobodyStanding);
    REQUIRE(clock.minutes() == GameClock{}.minutes());
}

TEST_CASE("every outcome says something", "[clock]") {
    const GameClock clock;
    REQUIRE_FALSE(rest_message(RestResult::Rested, clock).empty());
    REQUIRE_FALSE(rest_message(RestResult::Disturbed, clock).empty());
    REQUIRE_FALSE(rest_message(RestResult::NobodyStanding, clock).empty());
}
