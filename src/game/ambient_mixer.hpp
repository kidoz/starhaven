#ifndef STARHAVEN_GAME_AMBIENT_MIXER_HPP
#define STARHAVEN_GAME_AMBIENT_MIXER_HPP

// The sounds a place makes. A few decoration types name an ambient sound in
// `DDECLIST.BIN`, which `DSOUNDS.BIN` resolves to an entry of `Audio.snd`; a
// campfire crackles and a fountain runs. See docs/formats/dsounds.md.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/snd_archive.hpp"
#include "core/audio/wav.hpp"
#include "core/render/math3d.hpp"
#include "core/world/sound_table.hpp"

namespace starhaven::game {

// How far an ambient sound carries, in world units. A terrain cell is 512
// across, so this is a handful of cells — chosen by ear, not read from the
// data. `inferred`
inline constexpr float kAmbientRange = 2048.0f;

// One thing in the world that makes a noise.
struct AmbientSource {
    render::Vec3 position;
    std::uint32_t sound_id = 0;
};

// Mixes the ambient sounds audible from the listener's position.
//
// One looping stream per distinct sound id, its gain set from the nearest
// source. Sounds out of range are left at zero gain rather than torn down, so
// walking back towards a campfire does not restart it.
class AmbientMixer {
public:
    AmbientMixer() = default;
    AmbientMixer(const AmbientMixer&) = delete;
    AmbientMixer& operator=(const AmbientMixer&) = delete;
    ~AmbientMixer() { stop(); }

    // Open the sound archive. Returns false when the installation has none,
    // which leaves every call below a no-op.
    bool open(const std::filesystem::path& install) {
        return audio::SndArchive::open(install / "Sounds" / "Audio.snd", archive_) ==
               audio::SndError::None;
    }

    void update(const render::Vec3& listener, const std::vector<AmbientSource>& sources,
                const world::SoundTable& sounds) {
        // Nearest source wins for each distinct sound.
        std::map<std::uint32_t, float> gains;
        for (const auto& s : sources) {
            if (s.sound_id == 0)
                continue;
            const render::Vec3 d{s.position.x - listener.x, s.position.y - listener.y,
                                 s.position.z - listener.z};
            const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            const float gain = std::clamp(1.0f - distance / kAmbientRange, 0.0f, 1.0f);
            float& best = gains[s.sound_id];
            best = std::max(best, gain);
        }

        for (const auto& [id, gain] : gains) {
            Voice* voice = voice_for(id, sounds);
            if (voice == nullptr)
                continue;
            SDL_SetAudioStreamGain(voice->stream, gain);
            // Feeding only when the queue drains keeps the loop seamless
            // without buffering the whole map's audio ahead of time.
            if (gain > 0.0f && SDL_GetAudioStreamAvailable(voice->stream) == 0) {
                queue(*voice);
            }
        }
    }

    // Play a spell's own sound: the archive names its casts by Spells.txt
    // id — "04firebolt01" is Fire Bolt's row 4, "31townportal03" Town
    // Portal's 31, "21fly03" Fly's 21 — so the two-digit prefix is a join,
    // not a guess. The monster sets share the prefix space ("04barbarianB_
    // attack"), so a candidate must open with a letter after the digits and
    // carry none of the set-action suffixes. `observed` for the numbering
    // on the named examples; picking the first match is the engine's.
    void play_spell(int spell_id) {
        if (spell_id <= 0 || spell_id > 99) {
            return;
        }
        char prefix[3];
        std::snprintf(prefix, sizeof prefix, "%02d", spell_id);
        for (const auto& entry : archive_.entries()) {
            const std::string& name = entry.name;
            if (name.size() < 4 || name[0] != prefix[0] || name[1] != prefix[1]) {
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(name[2])) == 0) {
                continue;
            }
            if (name.find("_attack") != std::string::npos ||
                name.find("_die") != std::string::npos ||
                name.find("_charge") != std::string::npos ||
                name.find("_fidget") != std::string::npos ||
                name.find("Wince") != std::string::npos ||
                name.find("wince") != std::string::npos) {
                continue;
            }
            play_once(name);
            return;
        }
    }

    // Play one effect once, by its archive name — a door's creak, a lever's
    // clank. No script opcode names event sounds (a sweep of every unnamed
    // opcode's arguments against the sound table found none), so which name
    // plays when is the caller's own judgement. `inferred`
    void play_once(const std::string& name) {
        if (archive_.size() == 0) {
            return;
        }
        if (effect_.stream != nullptr) {
            SDL_DestroyAudioStream(effect_.stream);
            effect_ = {};
        }
        std::vector<std::uint8_t> riff;
        audio::WavAudio decoded;
        const std::size_t index = archive_.find(name);
        if (index < archive_.size() && archive_.read(index, riff) == audio::SndError::None &&
            audio::decode_wav(riff, decoded) == audio::WavError::None &&
            !decoded.samples.empty() && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec spec{SDL_AUDIO_S16LE, decoded.channels,
                                     static_cast<int>(decoded.sample_rate)};
            effect_.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                       nullptr, nullptr);
            if (effect_.stream != nullptr) {
                effect_.samples = std::move(decoded.samples);
                queue(effect_);
                SDL_ResumeAudioStreamDevice(effect_.stream);
            }
        }
    }

    // The open room's own soundtrack, fed a video frame's worth at a time
    // by the interior player. A format change reopens the stream; closing
    // the screen stops it.
    void play_room_chunk(const std::int16_t* samples, std::size_t count, int rate, bool stereo) {
        if (count == 0) {
            return;
        }
        if (room_.stream == nullptr || rate != room_rate_ || stereo != room_stereo_) {
            stop_room();
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                return;
            }
            const SDL_AudioSpec spec{SDL_AUDIO_S16LE, stereo ? 2 : 1, rate};
            room_.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                     nullptr, nullptr);
            if (room_.stream == nullptr) {
                return;
            }
            room_rate_ = rate;
            room_stereo_ = stereo;
            SDL_ResumeAudioStreamDevice(room_.stream);
        }
        SDL_PutAudioStreamData(room_.stream, samples,
                               static_cast<int>(count * sizeof(std::int16_t)));
    }

    void stop_room() {
        if (room_.stream != nullptr) {
            SDL_DestroyAudioStream(room_.stream);
            room_ = {};
        }
        room_rate_ = 0;
    }

    void stop() {
        stop_room();
        for (auto& [id, voice] : voices_) {
            if (voice.stream != nullptr) {
                SDL_DestroyAudioStream(voice.stream);
            }
        }
        if (effect_.stream != nullptr) {
            SDL_DestroyAudioStream(effect_.stream);
            effect_ = {};
        }
        if (!voices_.empty()) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        voices_.clear();
    }

    [[nodiscard]] std::size_t voice_count() const noexcept { return voices_.size(); }

private:
    struct Voice {
        SDL_AudioStream* stream = nullptr;
        std::vector<std::int16_t> samples;
    };

    static void queue(Voice& voice) {
        SDL_PutAudioStreamData(voice.stream, voice.samples.data(),
                               static_cast<int>(voice.samples.size() * sizeof(std::int16_t)));
        SDL_FlushAudioStream(voice.stream);
    }

    // Decode and open a stream the first time an id is heard. A failure is
    // remembered as an empty voice so the archive is probed once.
    Voice* voice_for(std::uint32_t id, const world::SoundTable& sounds) {
        if (const auto it = voices_.find(id); it != voices_.end()) {
            return it->second.stream == nullptr ? nullptr : &it->second;
        }
        Voice voice;
        const world::SoundTableEntry* entry = sounds.find(id);
        std::vector<std::uint8_t> riff;
        audio::WavAudio decoded;
        const std::size_t index = entry == nullptr ? archive_.size() : archive_.find(entry->name);
        if (index < archive_.size() && archive_.read(index, riff) == audio::SndError::None &&
            audio::decode_wav(riff, decoded) == audio::WavError::None && !decoded.samples.empty() &&
            SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec spec{SDL_AUDIO_S16LE, decoded.channels,
                                     static_cast<int>(decoded.sample_rate)};
            voice.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                     nullptr, nullptr);
            if (voice.stream != nullptr) {
                voice.samples = std::move(decoded.samples);
                SDL_SetAudioStreamGain(voice.stream, 0.0f);
                queue(voice);
                SDL_ResumeAudioStreamDevice(voice.stream);
            }
        }
        auto [it, _] = voices_.emplace(id, std::move(voice));
        return it->second.stream == nullptr ? nullptr : &it->second;
    }

    audio::SndArchive archive_;
    std::map<std::uint32_t, Voice> voices_;
    Voice effect_;  // the current one-shot; a new one replaces it
    Voice room_;    // the open interior's streaming soundtrack
    int room_rate_ = 0;
    bool room_stereo_ = false;
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_AMBIENT_MIXER_HPP
