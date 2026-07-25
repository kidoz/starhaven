#ifndef STARHAVEN_CORE_AUDIO_WAV_HPP
#define STARHAVEN_CORE_AUDIO_WAV_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace starhaven::audio {

// Outcome of decoding a RIFF/WAVE buffer. The decoder never throws.
enum class WavError {
    None,
    // Too short, or not a RIFF/WAVE container.
    NotWave,
    // A chunk header runs past the end of the buffer.
    Truncated,
    // No `fmt ` chunk, or one too short to describe the stream.
    BadFormat,
    // A format tag this decoder does not handle.
    UnsupportedFormat,
    // No `data` chunk.
    NoData,
    // The ADPCM block layout disagrees with the declared block size.
    BadBlockAlign,
};

// Decoded audio: interleaved 16-bit samples, whatever the source encoding, so
// callers have one format to feed a device.
struct WavAudio {
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 1;
};

// Decode a RIFF/WAVE buffer to 16-bit PCM.
//
// Handles the two encodings MM6's sound archive uses: plain PCM (tag 1) and
// IMA/DVI ADPCM (tag 17). ADPCM is a published standard, not a format specific
// to this game.
[[nodiscard]] WavError decode_wav(std::span<const std::uint8_t> data,
                                  WavAudio& out);

// Format tags, exposed for tests and tools.
constexpr std::uint16_t kWaveFormatPcm = 1;
constexpr std::uint16_t kWaveFormatImaAdpcm = 17;

}  // namespace starhaven::audio

#endif  // STARHAVEN_CORE_AUDIO_WAV_HPP
