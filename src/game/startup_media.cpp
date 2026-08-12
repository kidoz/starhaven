#include "game/startup_media.hpp"

#include <utility>

namespace starhaven::game {

std::optional<StartupMediaReport> StartupMediaController::begin(std::string_view reel,
                                                                std::span<const std::byte> bytes) {
    release();
    reel_ = std::string(reel);
    if (bytes.empty()) {
        return finish(StartupMediaOutcome::Unavailable);
    }

    video::SmackerDecoder decoder;
    if (video::SmackerDecoder::load(bytes, decoder) != video::SmackerError::None ||
        decoder.info().frame_count == 0) {
        return finish(StartupMediaOutcome::DecodeFailed);
    }

    decoder_ = std::move(decoder);
    frame_index_ = 0;
    active_ = true;
    return std::nullopt;
}

std::optional<StartupMediaReport> StartupMediaController::advance(StartupMediaFrame& frame) {
    frame = {};
    if (!active_) {
        return std::nullopt;
    }
    if (frame_index_ >= decoder_.info().frame_count) {
        return finish(StartupMediaOutcome::Finished);
    }

    std::span<const std::uint8_t> rgba;
    if (decoder_.decode_frame_rgba(frame_index_, rgba) != video::SmackerError::None) {
        return finish(StartupMediaOutcome::DecodeFailed);
    }

    audio_ = {};
    const video::SmackerAudioInfo audio_info = decoder_.audio_info(0);
    if (audio_info.present) {
        // Unsupported optional audio does not discard a valid picture. MM6's
        // observed startup reels use the supported DPCM form.
        if (decoder_.decode_audio(frame_index_, 0, audio_) != video::SmackerError::None) {
            audio_ = {};
        }
    }

    frame.rgba = rgba;
    frame.audio = audio_.samples;
    frame.width = decoder_.info().width;
    frame.height = decoder_.info().height;
    frame.audio_rate = audio_.sample_rate;
    frame.audio_channels = audio_.channels;
    frame.fps = decoder_.info().fps;
    ++frame_index_;
    return std::nullopt;
}

std::optional<StartupMediaReport> StartupMediaController::skip() {
    if (!active_) {
        return std::nullopt;
    }
    return finish(StartupMediaOutcome::Skipped);
}

void StartupMediaController::release() noexcept {
    reel_.clear();
    decoder_ = {};
    audio_ = {};
    frame_index_ = 0;
    active_ = false;
}

StartupMediaReport StartupMediaController::finish(StartupMediaOutcome outcome) {
    StartupMediaReport report{outcome, reel_};
    release();
    return report;
}

const char* startup_media_outcome_name(StartupMediaOutcome outcome) noexcept {
    switch (outcome) {
    case StartupMediaOutcome::Finished:
        return "finished";
    case StartupMediaOutcome::Skipped:
        return "skipped";
    case StartupMediaOutcome::Unavailable:
        return "unavailable";
    case StartupMediaOutcome::DecodeFailed:
        return "decode failed";
    }
    return "unavailable";
}

StartupAction startup_action_for_media_outcome(StartupMediaOutcome outcome) noexcept {
    switch (outcome) {
    case StartupMediaOutcome::Finished:
        return StartupAction::MediaFinished;
    case StartupMediaOutcome::Skipped:
        return StartupAction::MediaSkipped;
    case StartupMediaOutcome::Unavailable:
        return StartupAction::MediaUnavailable;
    case StartupMediaOutcome::DecodeFailed:
        return StartupAction::MediaDecodeFailed;
    }
    return StartupAction::MediaUnavailable;
}

}  // namespace starhaven::game
