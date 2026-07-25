#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/platform/paths.hpp"
#include "core/video/smacker.hpp"
#include "core/video/vid_archive.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--list] [<video-name>]\n"
              << "\n"
              << "Plays one Smacker video from your own legal game install's\n"
              << "Anims/*.vid archives. Video only — audio is not decoded yet.\n"
              << "\n"
              << "  --list             list every video in both archives\n"
              << "  --archive FILE     use one .vid file instead of both\n"
              << "  --frame N          decode a single frame and stop\n"
              << "  --screenshot FILE  write the decoded frame to a PPM and exit\n"
              << "  --scale N          integer window scale (default 2)\n"
              << "\n"
              << "Controls: SPACE pauses, ESC quits.\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory.\n";
}

// The two archives MM6 ships. Either may be absent from a partial install.
std::vector<std::filesystem::path> resolve_archives() {
    namespace fs = std::filesystem;
    std::vector<fs::path> found;
    if (auto install = starhaven::platform::install_from_env()) {
        for (const char* name : {"Anims1.vid", "Anims2.vid"}) {
            fs::path p = *install / "Anims" / name;
            if (fs::exists(p)) {
                found.push_back(p);
            }
        }
    }
    return found;
}

bool write_ppm(const std::string& path, std::span<const std::uint8_t> rgba,
               std::uint32_t width, std::uint32_t height) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        out.put(static_cast<char>(rgba[i * 4 + 0]));
        out.put(static_cast<char>(rgba[i * 4 + 1]));
        out.put(static_cast<char>(rgba[i * 4 + 2]));
    }
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    namespace video = starhaven::video;

    std::string want;
    std::string screenshot;
    std::string archive_override;
    bool list = false;
    long single_frame = -1;
    int scale = 2;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") {
            list = true;
        } else if (a == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (a == "--archive" && i + 1 < argc) {
            archive_override = argv[++i];
        } else if (a == "--frame" && i + 1 < argc) {
            single_frame = std::strtol(argv[++i], nullptr, 10);
        } else if (a == "--scale" && i + 1 < argc) {
            scale = std::max(1, static_cast<int>(std::strtol(argv[++i], nullptr, 10)));
        } else if (want.empty()) {
            want = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (!list && want.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    std::vector<std::filesystem::path> archives;
    if (!archive_override.empty()) {
        archives.emplace_back(archive_override);
    } else {
        archives = resolve_archives();
    }
    if (archives.empty()) {
        std::cerr << "error: no .vid archives found; set "
                  << starhaven::platform::kInstallEnvVar << "\n";
        return 1;
    }

    // Locate the requested video across the archives, or list them all.
    video::VidArchive found_archive;
    std::size_t found_index = 0;
    bool located = false;

    for (const auto& path : archives) {
        video::VidArchive archive;
        const video::VidError err = video::VidArchive::open(path, archive);
        if (err != video::VidError::None) {
            std::cerr << "warning: could not read " << path.filename().string() << "\n";
            continue;
        }
        if (list) {
            std::cout << path.filename().string() << ": " << archive.size()
                      << " videos\n";
            for (const auto& e : archive.entries()) {
                std::cout << "  " << e.name << "  (" << e.size << " bytes)\n";
            }
            continue;
        }
        if (const std::size_t i = archive.find(want); i != archive.size()) {
            found_archive = std::move(archive);
            found_index = i;
            located = true;
            break;
        }
    }
    if (list) {
        return 0;
    }
    if (!located) {
        std::cerr << "error: no video named " << want << "\n";
        return 1;
    }

    std::vector<std::byte> raw;
    if (!found_archive.read(found_index, raw)) {
        std::cerr << "error: could not read the video's bytes\n";
        return 1;
    }

    video::SmackerDecoder decoder;
    if (const video::SmackerError e = video::SmackerDecoder::load(raw, decoder);
        e != video::SmackerError::None) {
        std::cerr << "error: not a decodable Smacker video (code "
                  << static_cast<int>(e) << ")\n";
        return 1;
    }
    const video::SmackerInfo& info = decoder.info();
    std::cout << want << ": " << info.width << "x" << info.height << ", "
              << info.frame_count << " frames, " << info.fps << " fps"
              << (info.version4 ? ", SMK4" : ", SMK2")
              << (info.has_audio ? ", has audio (not decoded)" : "") << "\n";

    // Single-frame mode needs no window at all.
    if (!screenshot.empty() || single_frame >= 0) {
        const auto index = static_cast<std::uint32_t>(std::max(0L, single_frame));
        std::span<const std::uint8_t> rgba;
        if (decoder.decode_frame_rgba(index, rgba) != video::SmackerError::None) {
            std::cerr << "error: could not decode frame " << index << "\n";
            return 1;
        }
        if (!screenshot.empty()) {
            if (!write_ppm(screenshot, rgba, info.width, info.height)) {
                std::cerr << "error: could not write " << screenshot << "\n";
                return 1;
            }
            std::cout << "wrote " << screenshot << "\n";
        }
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "StarHaven — " + want;
    SDL_Window* window = SDL_CreateWindow(title.c_str(),
                                          static_cast<int>(info.width) * scale,
                                          static_cast<int>(info.height) * scale, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(info.width), static_cast<int>(info.height));

    const double frame_ms = (info.fps > 0.0) ? 1000.0 / info.fps : 100.0;
    std::uint64_t started = SDL_GetTicks();
    std::uint32_t shown = 0;
    bool paused = false;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_SPACE) {
                    paused = !paused;
                    // Re-anchor the clock so pausing does not fast-forward.
                    started = SDL_GetTicks() -
                              static_cast<std::uint64_t>(shown * frame_ms);
                }
            }
        }

        // Drive playback from the wall clock rather than counting loop
        // iterations, so a slow decode drops frames instead of slowing down.
        const auto elapsed = static_cast<double>(SDL_GetTicks() - started);
        const auto target = static_cast<std::uint32_t>(elapsed / frame_ms);
        if (!paused && target != shown) {
            if (target >= info.frame_count) {
                started = SDL_GetTicks();  // loop the video
                shown = 0;
            } else {
                shown = target;
            }
            std::span<const std::uint8_t> rgba;
            if (decoder.decode_frame_rgba(shown, rgba) == video::SmackerError::None) {
                SDL_UpdateTexture(texture, nullptr, rgba.data(),
                                  static_cast<int>(info.width) * 4);
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(5);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
