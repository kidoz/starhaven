#ifndef STARHAVEN_GAME_DAYLIGHT_HPP
#define STARHAVEN_GAME_DAYLIGHT_HPP

// What the sky looks like at this hour, and where the sun is.
//
// Nothing in the design tables says. What they do say is that there are hours
// and that establishments keep them, so an outdoor map has a time of day to
// show; everything below is this engine's reading of what that looks like.
// `inferred`

#include <cmath>

#include "core/render/color.hpp"
#include "core/render/math3d.hpp"
#include "game/clock.hpp"

namespace starhaven::game {

// When the sun is up. Sunrise and sunset an hour either side of these.
inline constexpr float kSunrise = 6.0f;
inline constexpr float kSunset = 20.0f;

// The hour as a fraction, so the sky moves through a minute rather than
// jumping on the hour.
[[nodiscard]] inline float hour_of(const GameClock& clock) noexcept {
    return static_cast<float>(clock.hour()) +
           static_cast<float>(clock.minute()) / static_cast<float>(kMinutesPerHour);
}

// How much daylight there is, from 0 in the dead of night to 1 at midday.
// Named for the clash it avoids: <time.h> declares a global `daylight`, and an
// unqualified call to one named that is ambiguous wherever both are in scope.
[[nodiscard]] inline float daylight_amount(const GameClock& clock) noexcept {
    const float hour = hour_of(clock);
    if (hour <= kSunrise - 1.0f || hour >= kSunset + 1.0f) {
        return 0.0f;
    }
    if (hour < kSunrise + 1.0f) {
        return (hour - (kSunrise - 1.0f)) * 0.5f;
    }
    if (hour > kSunset - 1.0f) {
        return ((kSunset + 1.0f) - hour) * 0.5f;
    }
    // Between the two, a gentle arc peaking at midday.
    const float noon = (kSunrise + kSunset) * 0.5f;
    const float from_noon = std::fabs(hour - noon) / (noon - kSunrise);
    return 1.0f - 0.25f * from_noon;
}

// Where the sun is: low in the east at dawn, overhead at noon, low in the west
// at dusk. Below the horizon it stops descending, so night keeps a direction
// to shade by rather than going flat.
[[nodiscard]] inline render::Vec3 sun_direction(const GameClock& clock) noexcept {
    const float hour = hour_of(clock);
    const float day = (hour - kSunrise) / (kSunset - kSunrise);  // 0 at dawn, 1 at dusk
    constexpr float kPi = 3.14159265f;
    const float angle = day * kPi;
    const float height = std::sin(angle);
    return render::normalize(render::Vec3{std::cos(angle), height < 0.15f ? 0.15f : height, 0.3f});
}

// And what colour the sky is: night, dawn, day, dusk, night.
[[nodiscard]] inline render::Color sky_colour(const GameClock& clock) noexcept {
    constexpr render::Color kNight{10, 12, 28, 255};
    constexpr render::Color kDay{135, 180, 220, 255};
    constexpr render::Color kEdge{200, 130, 90, 255};  // dawn and dusk

    const float light = daylight_amount(clock);
    const float hour = hour_of(clock);
    const bool edge = (hour > kSunrise - 1.5f && hour < kSunrise + 2.0f) ||
                      (hour > kSunset - 2.0f && hour < kSunset + 1.5f);

    const auto mix = [](const render::Color& a, const render::Color& b, float t) {
        const auto lerp = [t](std::uint8_t x, std::uint8_t y) {
            return static_cast<std::uint8_t>(static_cast<float>(x) +
                                             (static_cast<float>(y) - static_cast<float>(x)) * t);
        };
        return render::Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 255};
    };

    const render::Color plain = mix(kNight, kDay, light);
    return edge ? mix(plain, kEdge, 0.5f) : plain;
}

// How brightly to light the world, as a multiplier on the usual shading.
[[nodiscard]] inline float light_level(const GameClock& clock) noexcept {
    // Never fully black: a party that cannot see at all cannot play. `inferred`
    return 0.25f + 0.75f * daylight_amount(clock);
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_DAYLIGHT_HPP
