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

#include "core/audio/snd_archive.hpp"
#include "core/audio/wav.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/decoration_table.hpp"
#include "core/world/sound_table.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--list] [<sound-name>]\n"
              << "\n"
              << "Plays one sound effect from your own legal game install's\n"
              << "Sounds/Audio.snd archive.\n"
              << "\n"
              << "  --list             list every sound in the archive\n"
              << "  --table            list DSOUNDS.BIN and check its joins\n"
              << "  --id N             play the sound DSOUNDS.BIN gives id N\n"
              << "  --archive FILE     use a specific .snd file\n"
              << "  --dump FILE        write the decoded audio to a WAV\n"
              << "  --info             print the sound's format and stop\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
}

// The global sound table and the two tables that reference it. Loading it is
// separate from playback because a sound id, not a name, is what the rest of
// the game data carries.
bool load_sound_table(starhaven::world::SoundTable& sounds,
                      starhaven::world::DecorationTable& decorations) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    const auto install = starhaven::platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << starhaven::platform::kInstallEnvVar << "\n";
        return false;
    }
    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return false;
    }
    std::span<const std::byte> raw;
    if (icons.payload("DSOUNDS.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SoundTable::parse(raw, sounds) != world::SoundTableError::None) {
        std::cerr << "error: could not parse DSOUNDS.BIN\n";
        return false;
    }
    if (icons.payload("DDECLIST.BIN", raw) == lod::LodArchive::PayloadError::None) {
        (void)world::DecorationTable::parse(raw, decorations);
    }
    return true;
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
    const auto data_bytes = static_cast<std::uint32_t>(audio.samples.size() * sizeof(std::int16_t));
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    auto u16 = [&](std::uint16_t v) {
        for (int i = 0; i < 2; ++i)
            out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    out.write("RIFF", 4);
    u32(36 + data_bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(audio.channels);
    u32(audio.sample_rate);
    u32(audio.sample_rate * audio.channels * 2);
    u16(static_cast<std::uint16_t>(audio.channels * 2));
    u16(16);
    out.write("data", 4);
    u32(data_bytes);
    out.write(reinterpret_cast<const char*>(audio.samples.data()), data_bytes);
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    namespace audio = starhaven::audio;
    namespace world = starhaven::world;

    std::string want;
    std::string archive_path;
    std::string dump;
    bool list = false;
    bool table = false;
    long sound_id = -1;
    bool info_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--table") {
            table = true;
        } else if (a == "--id" && i + 1 < argc) {
            sound_id = static_cast<long>(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--list") {
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
    if (!list && !table && sound_id < 0 && want.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    audio::SndArchive archive;
    const std::filesystem::path path =
        archive_path.empty() ? resolve_archive() : std::filesystem::path{archive_path};
    if (const audio::SndError e = audio::SndArchive::open(path, archive);
        e != audio::SndError::None) {
        std::cerr << "error: could not open " << path.string() << " (code " << static_cast<int>(e)
                  << ")\n";
        return 1;
    }

    if (table) {
        world::SoundTable sounds;
        world::DecorationTable decorations;
        if (!load_sound_table(sounds, decorations)) {
            return 1;
        }
        std::size_t joined = 0;
        std::size_t named = 0;
        for (const auto& e : sounds.entries()) {
            if (e.name.empty())
                continue;
            ++named;
            if (archive.find(e.name) != archive.size())
                ++joined;
        }
        std::cout << sounds.size() << " sound records, " << named << " named\n";
        std::cout << "names that are Audio.snd entries: " << joined << "/" << named << "\n";

        std::size_t with_sound = 0;
        std::size_t resolved = 0;
        for (const auto& d : decorations.entries()) {
            if (d.sound_id == 0)
                continue;
            ++with_sound;
            const auto* s = sounds.find(d.sound_id);
            if (s != nullptr)
                ++resolved;
            std::cout << "  decoration " << d.name << " -> sound " << d.sound_id << " "
                      << (s != nullptr ? s->name : std::string{"(no such id)"}) << "\n";
        }
        std::cout << "decorations naming a sound: " << resolved << "/" << with_sound
                  << " resolve\n";
        return 0;
    }

    if (sound_id >= 0) {
        world::SoundTable sounds;
        world::DecorationTable decorations;
        if (!load_sound_table(sounds, decorations)) {
            return 1;
        }
        const auto* entry = sounds.find(static_cast<std::uint32_t>(sound_id));
        if (entry == nullptr) {
            std::cerr << "error: no sound has id " << sound_id << "\n";
            return 1;
        }
        std::cout << "id " << sound_id << " is " << entry->name << " (group " << entry->group
                  << ")\n";
        want = entry->name;
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
    if (const audio::WavError e = audio::decode_wav(riff, sound); e != audio::WavError::None) {
        std::cerr << "error: could not decode " << want << " (code " << static_cast<int>(e)
                  << ")\n";
        return 1;
    }

    const double seconds =
        sound.channels > 0 && sound.sample_rate > 0
            ? static_cast<double>(sound.samples.size()) / (sound.channels * sound.sample_rate)
            : 0.0;
    std::cout << want << ": " << sound.sample_rate << " Hz, "
              << (sound.channels == 2 ? "stereo" : "mono") << ", "
              << sound.samples.size() / std::max<std::uint16_t>(sound.channels, 1) << " frames ("
              << seconds << " s)\n";

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
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, sound.channels, static_cast<int>(sound.sample_rate)};
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        std::cerr << "error: could not open an audio device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_PutAudioStreamData(stream, sound.samples.data(),
                           static_cast<int>(sound.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);

    // Wait for the queue to drain rather than sleeping for a fixed time.
    while (SDL_GetAudioStreamAvailable(stream) > 0) {
        SDL_Delay(10);
    }
    SDL_Delay(150);  // let the device finish what it has buffered

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return 0;
}
