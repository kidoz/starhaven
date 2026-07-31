#ifndef STARHAVEN_GAME_CLOCK_HPP
#define STARHAVEN_GAME_CLOCK_HPP

// What time it is, and what that changes.
//
// The design tables know about time in three places: `2DEvents.txt` gives
// every establishment an opening and a closing hour, `PROFTEXT.txt` gives a
// hired NPC something different to say on each of the seven days of the week,
// and `MapStats.txt` says how many days a map takes to refill with monsters.
// So there are hours, there is a seven-day week, and there are days that
// count. How fast they pass no table says — but the executable does, and
// the rate below is now its own rather than this engine's.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace starhaven::game {

inline constexpr int kMinutesPerHour = 60;
inline constexpr int kHoursPerDay = 24;
inline constexpr int kMinutesPerDay = kMinutesPerHour * kHoursPerDay;

// `PROFTEXT.txt` has a column pair per day and there are seven of them, in this
// order. `observed`
inline constexpr std::array<std::string_view, 7> kWeekdays{
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// How much world time a second of walking about costs, traced. The world
// clock at `0x908d08` counts in units of which **a real second holds 128**
// — the sound code at `0x488d79` turns a table of plain seconds into them
// by multiplying by 128.0 — and the calendar routine at `0x4880a0` turns
// those units into world seconds by multiplying by the float at `0x4b9374`,
// **0.234375 = 30/128**, before dividing by 60, 60, 24 and 7. The two
// together say the world runs at **thirty times real time**: half a world
// minute a second, a world day in forty-eight minutes of sitting.
// `observed`
inline constexpr float kWorldSecondsPerSecond = 30.0f;
inline constexpr float kClockUnitsPerRealSecond = 128.0f;
inline constexpr float kMinutesPerSecond = kWorldSecondsPerSecond / 60.0f;

// The party starts on the morning of day one. `inferred`
inline constexpr std::int64_t kStartMinute = 9 * kMinutesPerHour;

// A point in game time, counted in minutes from the start of day one.
class GameClock {
public:
    GameClock() = default;
    explicit constexpr GameClock(std::int64_t minute) noexcept : minute_(minute) {}

    [[nodiscard]] std::int64_t minutes() const noexcept { return minute_; }
    [[nodiscard]] std::int64_t day() const noexcept { return minute_ / kMinutesPerDay; }
    [[nodiscard]] int hour() const noexcept {
        return static_cast<int>((minute_ % kMinutesPerDay) / kMinutesPerHour);
    }
    [[nodiscard]] int minute() const noexcept {
        return static_cast<int>(minute_ % kMinutesPerHour);
    }

    // Which of `PROFTEXT.txt`'s seven columns today is. Day one is a Sunday,
    // because the table lists Sunday first. `inferred`
    [[nodiscard]] std::string_view weekday() const noexcept {
        return kWeekdays[static_cast<std::size_t>(day() % 7)];
    }

    // Whether an establishment with these hours is open. A closing hour
    // before the opening one means it is open across midnight —
    // `2DEvents.txt` has taverns like that.
    //
    // Equal hours mean **no hours are recorded**, and such a place does not
    // close. Every one of the 404 rows that carries 0:00-0:00 is a house, a
    // dungeon or castle mouth, or a place you simply walk into — the City
    // Council, the Library, the Oracle, the Seer, a hermit's tent — and not
    // one trading counter among them. Reading equal hours as "shut" locked
    // the party out of the council and the library for good. `observed` in
    // the table, reproduced by `data_info --pace`.
    [[nodiscard]] bool open(int opens, int closes) const noexcept {
        if (opens == closes) {
            return true;
        }
        const int now = hour();
        return closes > opens ? now >= opens && now < closes : now >= opens || now < closes;
    }

    // The carry is kept in double on purpose. A frame is a fraction of a
    // minute, and accumulating those in float loses about a minute an hour —
    // a clock that runs slow by however often the engine happens to tick.
    void advance_seconds(float seconds) noexcept {
        carry_ += static_cast<double>(seconds) * kMinutesPerSecond;
        const auto whole = static_cast<std::int64_t>(carry_);
        if (whole > 0) {
            minute_ += whole;
            carry_ -= static_cast<double>(whole);
        }
    }

    void advance_hours(int hours) noexcept {
        minute_ += static_cast<std::int64_t>(hours) * kMinutesPerHour;
    }

    // "Day 3, 09:15, Tuesday" — for the corner of the screen.
    // The hour and minute alone, for a narrow spot.
    [[nodiscard]] std::string hhmm() const {
        const int h = hour();
        const int m = minute();
        return (h < 10 ? "0" : "") + std::to_string(h) + ":" +
               (m < 10 ? "0" : "") + std::to_string(m);
    }

    [[nodiscard]] std::string text() const {
        const int h = hour();
        const int m = minute();
        std::string out = "day " + std::to_string(day() + 1) + ", ";
        out += (h < 10 ? "0" : "") + std::to_string(h);
        out += ":";
        out += (m < 10 ? "0" : "") + std::to_string(m);
        out += ", ";
        out += weekday();
        return out;
    }

private:
    std::int64_t minute_ = kStartMinute;
    double carry_ = 0.0;  // time not yet worth a whole minute
};

// How long a rest takes. `inferred`
inline constexpr int kRestHours = 8;

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_CLOCK_HPP
