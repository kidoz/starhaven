#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/snd_archive.hpp"
#include "core/audio/wav.hpp"
#include "core/platform/paths.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--list] [<sound-name>]\n"
              << "\n"
              << "Plays one sound effect from your own legal game install's\n"
              << "Sounds/Audio.snd archive.\n"
              << "\n"
              << "  --list             list every sound in the archive\n"
              << "  --archive FILE     use a specific .snd file\n"
              << "  --dump FILE        write the decoded audio to a WAV\n"
              << "  --info             print the sound's format and stop\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory.\n";
}

std::filesystem::path resolve_archive() {
    namespace fs = std::filesystem;
    if (auto install = starhaven::platform::install_from_env()) {
        fs::path p = *install / "Sounds" / "Audio.snd";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "Sounds/Audio.snd";
}

bool write_wav(const std::string& path, const starhaven::audio::WavAudio& audio) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    const auto data_bytes =
        static_cast<std::uint32_t>(audio.samples.size() * sizeof(std::int16_t));
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8*i)) & 0xFF));
    };
    auto u16 = [&](std::uint16_t v) {
        for (int i = 0; i < 2; ++i) out.put(static_cast<char>((v >> (8*i)) & 0xFF));
    };
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(audio.channels);
    u32(audio.sample_rate);
    u32(audio.sample_rate * audio.channels * 2);
    u16(static_cast<std::uint16_t>(audio.channels * 2)); u16(16);
    out.write("data", 4); u32(data_bytes);
    out.write(reinterpret_cast<const char*>(audio.samples.data()), data_bytes);
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    namespace audio = starhaven::audio;

    std::string want;
    std::string archive_path;
    std::string dump;
    bool list = false;
    bool info_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") {
            list = true;
        } else if (a == "--info") {
            info_only = true;
        } else if (a == "--archive" && i + 1 < argc) {
            archive_path = argv[++i];
        } else if (a == "--dump" && i + 1 < argc) {
            dump = argv[++i];
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

    audio::SndArchive archive;
    const std::filesystem::path path =
        archive_path.empty() ? resolve_archive() : std::filesystem::path{archive_path};
    if (const audio::SndError e = audio::SndArchive::open(path, archive);
        e != audio::SndError::None) {
        std::cerr << "error: could not open " << path.string() << " (code "
                  << static_cast<int>(e) << ")\n";
        return 1;
    }

    if (list) {
        std::cout << archive.size() << " sounds\n";
        for (const auto& e : archive.entries()) {
            std::cout << "  " << e.name << "  (" << e.unpacked_size << " bytes"
                      << (e.stored() ? ", stored" : "") << ")\n";
        }
        return 0;
    }

    const std::size_t index = archive.find(want);
    if (index == archive.size()) {
        std::cerr << "error: no sound named " << want << "\n";
        return 1;
    }
    std::vector<std::uint8_t> riff;
    if (archive.read(index, riff) != audio::SndError::None) {
        std::cerr << "error: could not read " << want << "\n";
        return 1;
    }

    audio::WavAudio sound;
    if (const audio::WavError e = audio::decode_wav(riff, sound);
        e != audio::WavError::None) {
        std::cerr << "error: could not decode " << want << " (code "
                  << static_cast<int>(e) << ")\n";
        return 1;
    }

    const double seconds =
        sound.channels > 0 && sound.sample_rate > 0
            ? static_cast<double>(sound.samples.size()) /
                  (sound.channels * sound.sample_rate)
            : 0.0;
    std::cout << want << ": " << sound.sample_rate << " Hz, "
              << (sound.channels == 2 ? "stereo" : "mono") << ", "
              << sound.samples.size() / std::max<std::uint16_t>(sound.channels, 1)
              << " frames (" << seconds << " s)\n";

    if (!dump.empty()) {
        if (!write_wav(dump, sound)) {
            std::cerr << "error: could not write " << dump << "\n";
            return 1;
        }
        std::cout << "wrote " << dump << "\n";
        return 0;
    }
    if (info_only) {
        return 0;
    }

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, sound.channels,
                             static_cast<int>(sound.sample_rate)};
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        std::cerr << "error: could not open an audio device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_PutAudioStreamData(
        stream, sound.samples.data(),
        static_cast<int>(sound.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);

    // Wait for the queue to drain rather than sleeping for a fixed time.
    while (SDL_GetAudioStreamAvailable(stream) > 0) {
        SDL_Delay(10);
    }
    SDL_Delay(150);   // let the device finish what it has buffered

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return 0;
}
