#include "core/audio/mp3.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string>
#include <system_error>

// minimp3's implementation is compiled here and nowhere else.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

namespace starhaven::audio {

Mp3Error decode_mp3(std::span<const std::uint8_t> data, WavAudio& out) {
    out = WavAudio{};
    if (data.empty()) {
        return Mp3Error::NotMp3;
    }

    mp3dec_t decoder;
    mp3dec_init(&decoder);

    std::array<std::int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> frame{};
    std::size_t offset = 0;
    int channels = 0;
    int rate = 0;
    bool decoded_any = false;

    while (offset < data.size()) {
        mp3dec_frame_info_t info{};
        const int samples =
            mp3dec_decode_frame(&decoder, data.data() + offset,
                                static_cast<int>(data.size() - offset), frame.data(), &info);

        if (info.frame_bytes == 0) {
            break;  // no further frame could be located
        }
        offset += static_cast<std::size_t>(info.frame_bytes);

        if (samples <= 0) {
            continue;  // a skipped frame, such as an ID3 tag or junk
        }
        decoded_any = true;
        channels = info.channels;
        rate = info.hz;
        out.samples.insert(out.samples.end(), frame.begin(),
                           frame.begin() + static_cast<std::ptrdiff_t>(samples * info.channels));
    }

    if (!decoded_any) {
        return Mp3Error::NotMp3;
    }
    if (channels <= 0 || rate <= 0) {
        return Mp3Error::BadFormat;
    }
    out.channels = static_cast<std::uint16_t>(channels);
    out.sample_rate = static_cast<std::uint32_t>(rate);
    return Mp3Error::None;
}

Mp3Error decode_mp3_file(const std::filesystem::path& path, WavAudio& out) {
    out = WavAudio{};
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Mp3Error::Io;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end <= 0) {
        return Mp3Error::Io;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
        return Mp3Error::Io;
    }
    return decode_mp3(bytes, out);
}

std::vector<std::filesystem::path> find_music(const std::filesystem::path& install_dir) {
    namespace fs = std::filesystem;
    std::vector<fs::path> tracks;

    const fs::path sounds = install_dir / "Sounds";
    std::error_code ec;
    if (!fs::is_directory(sounds, ec)) {
        return tracks;
    }
    for (const auto& entry : fs::directory_iterator(sounds, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".mp3") {
            tracks.push_back(entry.path());
        }
    }
    // Numeric names sort as text, which puts 10 before 2; sort by the number
    // when both stems are numeric so the listing reads naturally.
    std::sort(tracks.begin(), tracks.end(), [](const fs::path& a, const fs::path& b) {
        const std::string sa = a.stem().string();
        const std::string sb = b.stem().string();
        const bool na = !sa.empty() && std::all_of(sa.begin(), sa.end(), ::isdigit);
        const bool nb = !sb.empty() && std::all_of(sb.begin(), sb.end(), ::isdigit);
        if (na && nb) {
            return std::stoi(sa) < std::stoi(sb);
        }
        return sa < sb;
    });
    return tracks;
}

}  // namespace starhaven::audio
