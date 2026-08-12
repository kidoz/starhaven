#include "game/startup_media_player.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/assets/asset_cache.hpp"
#include "game/ambient_mixer.hpp"
#include "game/startup_flow.hpp"
#include "game/startup_media.hpp"

namespace starhaven::game {

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr std::uint64_t kWarningMilliseconds = 1400;

[[nodiscard]] std::string warning_for(const StartupMediaReport& report) {
    if (report.outcome == StartupMediaOutcome::Unavailable) {
        return report.reel + " is unavailable; continuing.";
    }
    return report.reel + " could not be decoded; continuing.";
}

void destroy_picture(SDL_Texture*& picture) {
    if (picture != nullptr) {
        SDL_DestroyTexture(picture);
        picture = nullptr;
    }
}

}  // namespace

OpeningMediaResult play_opening_media(assets::AssetCache& cache, int window_scale) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return OpeningMediaResult::PlatformError;
    }

    SDL_Window* window =
        SDL_CreateWindow("StarHaven", kWidth * window_scale, kHeight * window_scale, 0);
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (window == nullptr || renderer == nullptr) {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return OpeningMediaResult::PlatformError;
    }
    SDL_SetRenderLogicalPresentation(renderer, kWidth, kHeight,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    StartupFlow flow;
    (void)flow.dispatch(StartupAction::ResourcesReady);
    StartupMediaController media;
    AmbientMixer audio;
    SDL_Texture* picture = nullptr;
    std::uint64_t next_frame_at = 0;
    std::uint64_t warning_until = 0;
    std::string warning;
    bool quit = false;

    const auto finish_report = [&](const StartupMediaReport& report) {
        audio.stop_room();
        destroy_picture(picture);
        next_frame_at = 0;
        (void)flow.dispatch(startup_action_for_media_outcome(report.outcome));
        if (report.outcome == StartupMediaOutcome::Unavailable ||
            report.outcome == StartupMediaOutcome::DecodeFailed) {
            warning = warning_for(report);
            warning_until = SDL_GetTicks() + kWarningMilliseconds;
        }
    };

    while (!quit && (flow.media_active() || SDL_GetTicks() < warning_until)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
                break;
            }
            if (media.active() &&
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
                if (const auto report = media.skip()) {
                    finish_report(*report);
                }
            }
        }
        if (quit) {
            break;
        }

        const std::uint64_t now = SDL_GetTicks();
        if (now >= warning_until && flow.media_active()) {
            if (!media.active()) {
                const std::string reel{flow.movie_name()};
                std::vector<std::byte> bytes;
                (void)cache.interior_bytes(reel, bytes);
                if (const auto report = media.begin(reel, bytes)) {
                    finish_report(*report);
                }
            }
            if (media.active() && now >= next_frame_at) {
                StartupMediaFrame frame;
                if (const auto report = media.advance(frame)) {
                    finish_report(*report);
                } else if (!frame.rgba.empty()) {
                    destroy_picture(picture);
                    picture = SDL_CreateTexture(
                        renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                        static_cast<int>(frame.width), static_cast<int>(frame.height));
                    if (picture == nullptr ||
                        !SDL_UpdateTexture(picture, nullptr, frame.rgba.data(),
                                           static_cast<int>(frame.width * 4))) {
                        const StartupMediaReport failed{StartupMediaOutcome::DecodeFailed,
                                                        std::string(media.reel())};
                        media.release();
                        finish_report(failed);
                    } else {
                        if (!frame.audio.empty()) {
                            audio.play_room_chunk(frame.audio.data(), frame.audio.size(),
                                                  static_cast<int>(frame.audio_rate),
                                                  frame.audio_channels == 2);
                        }
                        const double fps = frame.fps > 1.0 ? frame.fps : 15.0;
                        next_frame_at = now + static_cast<std::uint64_t>(1000.0 / fps);
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (picture != nullptr) {
            float width = 0.0f;
            float height = 0.0f;
            SDL_GetTextureSize(picture, &width, &height);
            const SDL_FRect destination{(kWidth - width) * 0.5f, (kHeight - height) * 0.5f, width,
                                        height};
            SDL_RenderTexture(renderer, picture, nullptr, &destination);
        }
        if (SDL_GetTicks() < warning_until) {
            SDL_SetRenderScale(renderer, 2.0f, 2.0f);
            SDL_SetRenderDrawColor(renderer, 238, 190, 126, 255);
            SDL_RenderDebugText(renderer, 20.0f, 110.0f, warning.c_str());
            SDL_SetRenderDrawColor(renderer, 210, 210, 205, 255);
            SDL_RenderDebugText(renderer, 20.0f, 126.0f,
                                "Startup will continue without this optional reel.");
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    media.release();
    audio.stop();
    destroy_picture(picture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return quit ? OpeningMediaResult::Quit : OpeningMediaResult::ReachedTitle;
}

}  // namespace starhaven::game
