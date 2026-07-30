#ifndef STARHAVEN_GAME_MUSIC_PLAYER_HPP
#define STARHAVEN_GAME_MUSIC_PLAYER_HPP

// The music that plays on a map. Which track is `MapStats.txt`'s business and
// arrives on the loaded session; this only plays it.

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/mp3.hpp"
#include "core/platform/paths.hpp"

namespace starhaven::game {

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
        SDL_SetAudioStreamGain(stream_, gain_);
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }

    // Re-queue the track when it runs out. Call once a frame; cheap enough to
    // do unconditionally.
    // The install's own LoudMusic switch: 1 is full, 0 the quieter mix.
    void set_loud(bool loud) {
        gain_ = loud ? 1.0f : 0.5f;
        if (stream_ != nullptr) {
            SDL_SetAudioStreamGain(stream_, gain_);
        }
    }

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
    float gain_ = 1.0f;
    std::vector<std::int16_t> samples_;
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_MUSIC_PLAYER_HPP
