#include "core/audio/wav.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace starhaven::audio {

namespace {

// IMA/DVI ADPCM reconstruction tables. These are the published constants of
// the IMA standard, not anything specific to this game.
constexpr std::array<int, 89> kStepTable = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};

constexpr std::array<int, 16> kIndexTable = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

// One channel's running ADPCM state.
struct AdpcmState {
    int predictor = 0;
    int index = 0;
};

[[nodiscard]] std::int16_t adpcm_step(AdpcmState& s, std::uint8_t nibble) {
    const int step = kStepTable[static_cast<std::size_t>(s.index)];

    // The magnitude is built from the nibble's three low bits, with the usual
    // step/8 rounding term.
    int diff = step >> 3;
    if ((nibble & 1) != 0) diff += step >> 2;
    if ((nibble & 2) != 0) diff += step >> 1;
    if ((nibble & 4) != 0) diff += step;
    if ((nibble & 8) != 0) {
        s.predictor -= diff;
    } else {
        s.predictor += diff;
    }
    s.predictor = std::clamp(s.predictor, -32768, 32767);

    s.index = std::clamp(s.index + kIndexTable[nibble & 0x0F], 0, 88);
    return static_cast<std::int16_t>(s.predictor);
}

[[nodiscard]] std::uint32_t u32_at(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) |
           (static_cast<std::uint32_t>(d[o + 1]) << 8) |
           (static_cast<std::uint32_t>(d[o + 2]) << 16) |
           (static_cast<std::uint32_t>(d[o + 3]) << 24);
}

[[nodiscard]] std::uint16_t u16_at(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint16_t>(d[o] | (d[o + 1] << 8));
}

}  // namespace

WavError decode_wav(std::span<const std::uint8_t> data, WavAudio& out) {
    out = WavAudio{};

    constexpr std::size_t kRiffHeader = 12;
    if (data.size() < kRiffHeader ||
        std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return WavError::NotWave;
    }

    // Walk the chunk list rather than assuming `fmt ` and `data` sit at fixed
    // offsets: MM6's entries carry a `fact` chunk between them.
    std::span<const std::uint8_t> fmt;
    std::span<const std::uint8_t> payload;
    // `fact` states the exact frame count. ADPCM blocks are padded out to whole
    // groups of eight, so without it the tail carries up to seven invented
    // frames.
    bool have_fact = false;
    std::uint32_t fact_frames = 0;
    std::size_t pos = kRiffHeader;
    while (pos + 8 <= data.size()) {
        const std::size_t size = u32_at(data, pos + 4);
        const std::size_t body = pos + 8;
        if (size > data.size() - body) {
            return WavError::Truncated;
        }
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0) {
            fmt = data.subspan(body, size);
        } else if (std::memcmp(data.data() + pos, "data", 4) == 0) {
            payload = data.subspan(body, size);
        } else if (std::memcmp(data.data() + pos, "fact", 4) == 0 && size >= 4) {
            have_fact = true;
            fact_frames = u32_at(data, body);
        }
        // Chunks are padded to an even length.
        pos = body + size + (size & 1u);
    }

    if (fmt.size() < 16) {
        return WavError::BadFormat;
    }
    const std::uint16_t tag = u16_at(fmt, 0);
    const std::uint16_t channels = u16_at(fmt, 2);
    const std::uint32_t rate = u32_at(fmt, 4);
    const std::uint16_t block_align = u16_at(fmt, 12);
    const std::uint16_t bits = u16_at(fmt, 14);
    if (channels == 0 || channels > 2 || rate == 0) {
        return WavError::BadFormat;
    }
    if (payload.empty()) {
        return WavError::NoData;
    }

    out.sample_rate = rate;
    out.channels = channels;

    if (tag == kWaveFormatPcm) {
        if (bits == 16) {
            out.samples.resize(payload.size() / 2);
            for (std::size_t i = 0; i < out.samples.size(); ++i) {
                out.samples[i] = static_cast<std::int16_t>(u16_at(payload, i * 2));
            }
        } else if (bits == 8) {
            // Eight-bit PCM is unsigned, centred on 128.
            out.samples.resize(payload.size());
            for (std::size_t i = 0; i < payload.size(); ++i) {
                out.samples[i] =
                    static_cast<std::int16_t>((static_cast<int>(payload[i]) - 128) << 8);
            }
        } else {
            return WavError::UnsupportedFormat;
        }
        return WavError::None;
    }

    if (tag != kWaveFormatImaAdpcm) {
        return WavError::UnsupportedFormat;
    }

    // IMA ADPCM is blocked: each block opens with a per-channel header giving
    // the starting predictor and step index, then packs nibbles in groups of
    // four bytes per channel.
    constexpr std::size_t kHeaderPerChannel = 4;
    const std::size_t header_bytes = kHeaderPerChannel * channels;
    if (block_align <= header_bytes) {
        return WavError::BadBlockAlign;
    }
    const std::size_t nibble_bytes = block_align - header_bytes;
    const std::size_t group_bytes = 4 * static_cast<std::size_t>(channels);
    if (nibble_bytes % group_bytes != 0) {
        return WavError::BadBlockAlign;
    }

    std::array<AdpcmState, 2> state{};
    for (std::size_t base = 0; base < payload.size(); base += block_align) {
        const std::size_t avail = std::min<std::size_t>(block_align, payload.size() - base);
        if (avail < header_bytes) {
            break;  // a partial block carries no usable header
        }
        for (std::uint16_t c = 0; c < channels; ++c) {
            const std::size_t o = base + c * kHeaderPerChannel;
            state[c].predictor = static_cast<std::int16_t>(u16_at(payload, o));
            state[c].index = std::clamp<int>(payload[o + 2], 0, 88);
            // The block's first sample is the predictor itself.
            out.samples.push_back(static_cast<std::int16_t>(state[c].predictor));
        }

        // Groups of four bytes per channel, low nibble first.
        const std::size_t groups = (avail - header_bytes) / group_bytes;
        for (std::size_t g = 0; g < groups; ++g) {
            for (std::uint16_t c = 0; c < channels; ++c) {
                const std::size_t o =
                    base + header_bytes + (g * channels + c) * std::size_t{4};
                for (std::size_t k = 0; k < 4; ++k) {
                    const std::uint8_t byte = payload[o + k];
                    // Two samples per byte; interleaving is restored below.
                    out.samples.push_back(adpcm_step(state[c], byte & 0x0F));
                    out.samples.push_back(adpcm_step(state[c], byte >> 4));
                }
            }
        }
    }

    // For stereo the loop above emits eight samples of one channel before the
    // other's; re-interleave so callers see L,R,L,R.
    if (channels == 2) {
        const std::size_t per_block_samples =
            1 + (nibble_bytes / group_bytes) * std::size_t{8};
        std::vector<std::int16_t> fixed;
        fixed.reserve(out.samples.size());
        const std::size_t block_samples = per_block_samples * 2;
        for (std::size_t b = 0; b + block_samples <= out.samples.size();
             b += block_samples) {
            for (std::size_t i = 0; i < per_block_samples; ++i) {
                // Channel 0's run starts after the two leading predictors.
                const std::size_t left = (i == 0) ? b : b + 2 + (i - 1);
                const std::size_t right =
                    (i == 0) ? b + 1 : b + 2 + (per_block_samples - 1) + (i - 1);
                if (right >= out.samples.size()) break;
                fixed.push_back(out.samples[left]);
                fixed.push_back(out.samples[right]);
            }
        }
        out.samples = std::move(fixed);
    }

    // Trim the block padding. Across MM6's 1,526 sounds the decoded length
    // always lands within seven frames above this figure, never below it.
    if (have_fact) {
        const std::size_t exact =
            static_cast<std::size_t>(fact_frames) * channels;
        if (exact <= out.samples.size()) {
            out.samples.resize(exact);
        }
    }
    return WavError::None;
}

}  // namespace starhaven::audio
