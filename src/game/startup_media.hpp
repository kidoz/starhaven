#ifndef STARHAVEN_GAME_STARTUP_MEDIA_HPP
#define STARHAVEN_GAME_STARTUP_MEDIA_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/video/smacker.hpp"
#include "game/startup_flow.hpp"

namespace starhaven::game {

enum class StartupMediaOutcome : std::uint8_t {
    Finished,
    Skipped,
    Unavailable,
    DecodeFailed,
};

struct StartupMediaReport {
    StartupMediaOutcome outcome = StartupMediaOutcome::Unavailable;
    std::string reel;
};

struct StartupMediaFrame {
    std::span<const std::uint8_t> rgba;
    std::span<const std::int16_t> audio;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t audio_rate = 0;
    std::uint8_t audio_channels = 1;
    double fps = 0.0;
};

// Owns one optional startup reel at a time. Resource lookup, clocks, SDL
// textures and audio devices remain adapter responsibilities; decoder and
// decoded-frame lifetime end together on every terminal outcome.
class StartupMediaController {
public:
    StartupMediaController() = default;
    ~StartupMediaController() = default;
    StartupMediaController(const StartupMediaController&) = delete;
    StartupMediaController& operator=(const StartupMediaController&) = delete;
    StartupMediaController(StartupMediaController&&) noexcept = default;
    StartupMediaController& operator=(StartupMediaController&&) noexcept = default;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::string_view reel() const noexcept { return reel_; }

    // Empty bytes mean that the optional reel could not be located. Malformed
    // bytes report DecodeFailed. Success has no immediate report.
    [[nodiscard]] std::optional<StartupMediaReport> begin(std::string_view reel,
                                                          std::span<const std::byte> bytes);

    // Decode the next picture and its first audio track. Natural completion is
    // reported on the call after the final frame so that adapters can present
    // that final picture for one full frame interval.
    [[nodiscard]] std::optional<StartupMediaReport> advance(StartupMediaFrame& frame);
    [[nodiscard]] std::optional<StartupMediaReport> skip();

    void release() noexcept;

private:
    [[nodiscard]] StartupMediaReport finish(StartupMediaOutcome outcome);

    std::string reel_;
    video::SmackerDecoder decoder_;
    video::SmackerAudioFrame audio_;
    std::uint32_t frame_index_ = 0;
    bool active_ = false;
};

[[nodiscard]] const char* startup_media_outcome_name(StartupMediaOutcome outcome) noexcept;
[[nodiscard]] StartupAction startup_action_for_media_outcome(StartupMediaOutcome outcome) noexcept;

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_STARTUP_MEDIA_HPP
