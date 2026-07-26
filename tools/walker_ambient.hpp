#ifndef STARHAVEN_TOOLS_WALKER_AMBIENT_HPP
#define STARHAVEN_TOOLS_WALKER_AMBIENT_HPP

// The sounds a place makes. A few decoration types name an ambient sound in
// `DDECLIST.BIN`, which `DSOUNDS.BIN` resolves to an entry of `Audio.snd`; a
// campfire crackles and a fountain runs. See docs/formats/dsounds.md.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

#include <SDL3/SDL.h>

#include "core/audio/snd_archive.hpp"
#include "core/audio/wav.hpp"
#include "core/render/math3d.hpp"
#include "core/world/sound_table.hpp"

namespace starhaven::tools {

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

    void stop() {
        for (auto& [id, voice] : voices_) {
            if (voice.stream != nullptr) {
                SDL_DestroyAudioStream(voice.stream);
            }
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
};

}  // namespace starhaven::tools

#endif  // STARHAVEN_TOOLS_WALKER_AMBIENT_HPP
