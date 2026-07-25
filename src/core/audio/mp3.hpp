#ifndef STARHAVEN_CORE_AUDIO_MP3_HPP
#define STARHAVEN_CORE_AUDIO_MP3_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "core/audio/wav.hpp"

namespace starhaven::audio {

// Outcome of decoding an MP3. The decoder never throws.
enum class Mp3Error {
    None,
    // The file could not be opened or read.
    Io,
    // No decodable MP3 frame was found.
    NotMp3,
    // Frames decoded, but the stream declared no channels or sample rate.
    BadFormat,
};

// Decode an MP3 buffer to interleaved 16-bit PCM.
//
// MM6's music is ordinary MP3, not a game-specific format, so this wraps a
// third-party decoder (minimp3, public domain) rather than reverse
// engineering anything. The result uses the same WavAudio shape the sound
// effects produce, so callers feed one format to a device.
[[nodiscard]] Mp3Error decode_mp3(std::span<const std::uint8_t> data, WavAudio& out);

// Read a file and decode it.
[[nodiscard]] Mp3Error decode_mp3_file(const std::filesystem::path& path, WavAudio& out);

// The music tracks shipped beside a game installation, sorted by name.
// Returns an empty list when the directory does not exist.
[[nodiscard]] std::vector<std::filesystem::path>
find_music(const std::filesystem::path& install_dir);

}  // namespace starhaven::audio

#endif  // STARHAVEN_CORE_AUDIO_MP3_HPP
