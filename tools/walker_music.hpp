#ifndef STARHAVEN_TOOLS_WALKER_MUSIC_HPP
#define STARHAVEN_TOOLS_WALKER_MUSIC_HPP

// What a walker needs to know about the map it just loaded, beyond geometry:
// the name the designers gave it and the music that plays there. Both come out
// of `MapStats.txt` (see docs/formats/text-tables.md).

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/mp3.hpp"
#include "core/data/game_data.hpp"
#include "core/data/map_stats.hpp"
#include "core/platform/paths.hpp"

namespace starhaven::tools {

// A map's entry in the design table, or empty values when the installation has
// no table or does not list this map.
struct MapIdentity {
    std::string display_name;
    int music_track = 0;
};

inline MapIdentity identify_map(const std::string& map_file_name) {
    MapIdentity out;
    const auto install = platform::install_from_env();
    if (!install) {
        return out;
    }
    data::MapStatsTable maps;
    if (data::load_map_stats(*install / "data", maps) != data::GameDataError::None) {
        return out;
    }
    const data::MapStatsEntry* e = maps.find(map_file_name);
    if (e == nullptr) {
        return out;
    }
    out.display_name = data::cp1252_to_utf8(e->name);
    out.music_track = e->music_track;
    return out;
}

// Plays one music track on a loop while a walker runs.
//
// The track is decoded once and held in memory, as the player tool does: a
// track is a few tens of megabytes of PCM, which is cheaper than streaming and
// keeps the walker's frame loop free of audio work.
class MusicPlayer {
public:
    MusicPlayer() = default;
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    ~MusicPlayer() { stop(); }

    // Start `Sounds/<track>.mp3`. Returns false when the track number is not
    // one of the shipped files, the file will not decode, or no audio device
    // opens — none of which should stop a walker from running.
    bool start(const std::filesystem::path& install, int track) {
        stop();
        if (track <= 0) {
            return false;
        }
        const std::filesystem::path path = install / "Sounds" / (std::to_string(track) + ".mp3");
        audio::WavAudio music;
        if (audio::decode_mp3_file(path, music) != audio::Mp3Error::None) {
            return false;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            return false;
        }
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, music.channels,
                                 static_cast<int>(music.sample_rate)};
        stream_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        samples_ = std::move(music.samples);
        queue();
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }

    // Re-queue the track when it runs out. Call once a frame; cheap enough to
    // do unconditionally.
    void update() {
        if (stream_ != nullptr && SDL_GetAudioStreamAvailable(stream_) == 0) {
            queue();
        }
    }

    void stop() {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        samples_.clear();
    }

private:
    void queue() {
        if (samples_.empty())
            return;
        SDL_PutAudioStreamData(stream_, samples_.data(),
                               static_cast<int>(samples_.size() * sizeof(std::int16_t)));
        SDL_FlushAudioStream(stream_);
    }

    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::int16_t> samples_;
};

}  // namespace starhaven::tools

#endif  // STARHAVEN_TOOLS_WALKER_MUSIC_HPP
