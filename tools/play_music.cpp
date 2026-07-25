#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/mp3.hpp"
#include "core/platform/paths.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--list] [<track>]\n"
              << "\n"
              << "Plays one of the music tracks shipped with your own legal\n"
              << "game installation. `track` is a file name or its stem.\n"
              << "\n"
              << "  --list             list the tracks found\n"
              << "  --info             print the track's format and stop\n"
              << "  --seconds N        stop after N seconds\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
}

}  // namespace

int main(int argc, char** argv) {
    namespace audio = starhaven::audio;
    namespace fs = std::filesystem;

    std::string want;
    bool list = false;
    bool info_only = false;
    double limit_seconds = 0.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") {
            list = true;
        } else if (a == "--info") {
            info_only = true;
        } else if (a == "--seconds" && i + 1 < argc) {
            limit_seconds = std::strtod(argv[++i], nullptr);
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

    const auto install = starhaven::platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << starhaven::platform::kInstallEnvVar << "\n";
        return 1;
    }
    const auto tracks = audio::find_music(*install);
    if (tracks.empty()) {
        std::cerr << "error: no .mp3 tracks under " << (*install / "Sounds").string() << "\n";
        return 1;
    }

    if (list) {
        std::cout << tracks.size() << " tracks\n";
        for (const auto& t : tracks) {
            std::cout << "  " << t.filename().string() << "\n";
        }
        return 0;
    }

    // Accept either "10" or "10.mp3".
    const auto match = std::find_if(tracks.begin(), tracks.end(), [&](const fs::path& p) {
        return p.filename().string() == want || p.stem().string() == want;
    });
    if (match == tracks.end()) {
        std::cerr << "error: no track named " << want << "\n";
        return 1;
    }

    audio::WavAudio music;
    if (const audio::Mp3Error e = audio::decode_mp3_file(*match, music);
        e != audio::Mp3Error::None) {
        std::cerr << "error: could not decode " << match->filename().string() << " (code "
                  << static_cast<int>(e) << ")\n";
        return 1;
    }

    const double seconds =
        music.channels > 0 && music.sample_rate > 0
            ? static_cast<double>(music.samples.size()) / (music.channels * music.sample_rate)
            : 0.0;
    std::cout << match->filename().string() << ": " << music.sample_rate << " Hz, "
              << (music.channels == 2 ? "stereo" : "mono") << ", " << seconds << " s\n";
    if (info_only) {
        return 0;
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, music.channels, static_cast<int>(music.sample_rate)};
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        std::cerr << "error: could not open an audio device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_PutAudioStreamData(stream, music.samples.data(),
                           static_cast<int>(music.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);

    const std::uint64_t started = SDL_GetTicks();
    while (SDL_GetAudioStreamAvailable(stream) > 0) {
        if (limit_seconds > 0.0 &&
            static_cast<double>(SDL_GetTicks() - started) / 1000.0 >= limit_seconds) {
            break;
        }
        SDL_Delay(20);
    }

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return 0;
}
