// Tests for the RIFF/WAVE decoder, including IMA ADPCM.
//
// Fixtures are SYNTHETIC: every buffer is assembled here. No bytes from the
// game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/audio/wav.hpp"

using namespace starhaven::audio;

namespace {

void push_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8*i)) & 0xFF));
}
void push_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    for (int i = 0; i < 2; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8*i)) & 0xFF));
}
void push_tag(std::vector<std::uint8_t>& v, const char* t) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(t[i]));
}

// Assemble a RIFF/WAVE buffer from a format chunk body and sample data.
std::vector<std::uint8_t> make_wav(std::uint16_t tag, std::uint16_t channels,
                                   std::uint32_t rate, std::uint16_t block_align,
                                   std::uint16_t bits,
                                   const std::vector<std::uint8_t>& data,
                                   bool with_fact = false,
                                   std::uint32_t fact_frames = 0) {
    std::vector<std::uint8_t> fmt;
    push_u16(fmt, tag);
    push_u16(fmt, channels);
    push_u32(fmt, rate);
    push_u32(fmt, rate * channels);   // byte rate; unused by the decoder
    push_u16(fmt, block_align);
    push_u16(fmt, bits);

    std::vector<std::uint8_t> body;
    push_tag(body, "WAVE");
    push_tag(body, "fmt ");
    push_u32(body, static_cast<std::uint32_t>(fmt.size()));
    body.insert(body.end(), fmt.begin(), fmt.end());
    if (with_fact) {
        push_tag(body, "fact");
        push_u32(body, 4);
        push_u32(body, fact_frames);
    }
    push_tag(body, "data");
    push_u32(body, static_cast<std::uint32_t>(data.size()));
    body.insert(body.end(), data.begin(), data.end());

    std::vector<std::uint8_t> out;
    push_tag(out, "RIFF");
    push_u32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace

TEST_CASE("16-bit PCM decodes unchanged", "[wav]") {
    std::vector<std::uint8_t> data;
    push_u16(data, static_cast<std::uint16_t>(0));
    push_u16(data, static_cast<std::uint16_t>(1000));
    push_u16(data, static_cast<std::uint16_t>(0xFC18));  // -1000

    auto buf = make_wav(kWaveFormatPcm, 1, 22050, 2, 16, data);
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::None);
    REQUIRE(audio.sample_rate == 22050);
    REQUIRE(audio.channels == 1);
    REQUIRE(audio.samples == std::vector<std::int16_t>{0, 1000, -1000});
}

TEST_CASE("8-bit PCM is recentred, not reinterpreted", "[wav]") {
    // Eight-bit WAV samples are unsigned with 128 as silence.
    const std::vector<std::uint8_t> data = {128, 129, 127, 255, 0};
    auto buf = make_wav(kWaveFormatPcm, 1, 11025, 1, 8, data);
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::None);
    REQUIRE(audio.samples[0] == 0);
    REQUIRE(audio.samples[1] == 256);
    REQUIRE(audio.samples[2] == -256);
    REQUIRE(audio.samples[3] == 127 * 256);
    REQUIRE(audio.samples[4] == -128 * 256);
}

TEST_CASE("IMA ADPCM reconstructs the documented step sequence", "[wav]") {
    // One mono block: predictor 0, index 0, then nibbles.
    // With step 7, nibble 4 adds step -> 7 and moves the index to 2;
    // with step 9, nibble 0 adds step/8 -> 8 and moves the index back to 1.
    std::vector<std::uint8_t> data;
    push_u16(data, 0);        // predictor
    data.push_back(0);        // step index
    data.push_back(0);        // padding
    data.push_back(0x04);     // nibbles 4 then 0
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x00);

    auto buf = make_wav(kWaveFormatImaAdpcm, 1, 22050, /*block_align*/ 8, 4, data);
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::None);
    REQUIRE(audio.samples.size() == 9);   // the predictor plus eight nibbles
    REQUIRE(audio.samples[0] == 0);
    REQUIRE(audio.samples[1] == 7);
    REQUIRE(audio.samples[2] == 8);
}

TEST_CASE("the fact chunk trims ADPCM block padding", "[wav]") {
    std::vector<std::uint8_t> data;
    push_u16(data, 0);
    data.push_back(0);
    data.push_back(0);
    for (int i = 0; i < 4; ++i) data.push_back(0);

    // The block yields nine frames; the fact chunk says only five are real.
    auto buf = make_wav(kWaveFormatImaAdpcm, 1, 22050, 8, 4, data,
                        /*with_fact*/ true, /*fact_frames*/ 5);
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::None);
    REQUIRE(audio.samples.size() == 5);
}

TEST_CASE("a fact chunk longer than the data does not extend it", "[wav]") {
    std::vector<std::uint8_t> data;
    push_u16(data, 0);
    data.push_back(0);
    data.push_back(0);
    for (int i = 0; i < 4; ++i) data.push_back(0);
    auto buf = make_wav(kWaveFormatImaAdpcm, 1, 22050, 8, 4, data, true, 9999);
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::None);
    REQUIRE(audio.samples.size() == 9);   // unchanged, not padded out
}

TEST_CASE("a non-RIFF buffer is rejected", "[wav]") {
    const std::vector<std::uint8_t> junk(64, 0xAB);
    WavAudio audio;
    REQUIRE(decode_wav(junk, audio) == WavError::NotWave);
    REQUIRE(decode_wav({}, audio) == WavError::NotWave);
}

TEST_CASE("a chunk running past the buffer is rejected", "[wav]") {
    auto buf = make_wav(kWaveFormatPcm, 1, 22050, 2, 16, {0, 0, 0, 0});
    // Inflate the data chunk's declared size beyond what is present.
    const std::size_t data_size_at = buf.size() - 4 - 4;
    buf[data_size_at] = 0xFF;
    buf[data_size_at + 1] = 0xFF;
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::Truncated);
}

TEST_CASE("an unknown format tag is reported, not guessed at", "[wav]") {
    auto buf = make_wav(/*tag*/ 85, 1, 22050, 2, 16, {0, 0});   // MP3
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::UnsupportedFormat);
}

TEST_CASE("a wave without a data chunk is rejected", "[wav]") {
    std::vector<std::uint8_t> fmt;
    push_u16(fmt, kWaveFormatPcm); push_u16(fmt, 1); push_u32(fmt, 22050);
    push_u32(fmt, 44100); push_u16(fmt, 2); push_u16(fmt, 16);
    std::vector<std::uint8_t> body;
    push_tag(body, "WAVE"); push_tag(body, "fmt ");
    push_u32(body, static_cast<std::uint32_t>(fmt.size()));
    body.insert(body.end(), fmt.begin(), fmt.end());
    std::vector<std::uint8_t> buf;
    push_tag(buf, "RIFF"); push_u32(buf, static_cast<std::uint32_t>(body.size()));
    buf.insert(buf.end(), body.begin(), body.end());

    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::NoData);
}

TEST_CASE("an ADPCM block size that cannot hold a header is rejected", "[wav]") {
    auto buf = make_wav(kWaveFormatImaAdpcm, 1, 22050, /*block_align*/ 2, 4,
                        {0, 0, 0, 0});
    WavAudio audio;
    REQUIRE(decode_wav(buf, audio) == WavError::BadBlockAlign);
}
