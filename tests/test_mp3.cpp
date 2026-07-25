// Tests for the MP3 wrapper and music discovery.
//
// The decode path itself is third-party (minimp3); what is tested here is this
// project's behaviour around it: rejection of non-MP3 input, and how tracks are
// found and ordered. Decode correctness is checked against the real tracks by
// signal statistics, which a unit test cannot synthesise without an encoder.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "core/audio/mp3.hpp"

using namespace starhaven::audio;
namespace fs = std::filesystem;

namespace {

// A scratch directory that cleans itself up.
class TempDir {
public:
    TempDir() {
        base_ = fs::temp_directory_path() / ("starhaven_mp3_test_" + std::to_string(::getpid()));
        fs::create_directories(base_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(base_, ec);
    }
    [[nodiscard]] const fs::path& path() const { return base_; }

    void write(const fs::path& relative, const std::string& contents) const {
        const fs::path full = base_ / relative;
        fs::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out << contents;
    }

private:
    fs::path base_;
};

}  // namespace

TEST_CASE("an empty buffer is not an MP3", "[mp3]") {
    WavAudio audio;
    REQUIRE(decode_mp3({}, audio) == Mp3Error::NotMp3);
}

TEST_CASE("random bytes are not an MP3", "[mp3]") {
    const std::vector<std::uint8_t> junk(4096, 0xA5);
    WavAudio audio;
    REQUIRE(decode_mp3(junk, audio) == Mp3Error::NotMp3);
    REQUIRE(audio.samples.empty());
}

TEST_CASE("a missing file is reported as an I/O failure", "[mp3]") {
    WavAudio audio;
    REQUIRE(decode_mp3_file("/nonexistent/starhaven/track.mp3", audio) == Mp3Error::Io);
}

TEST_CASE("an empty file is reported rather than decoded", "[mp3]") {
    TempDir dir;
    dir.write("empty.mp3", "");
    WavAudio audio;
    REQUIRE(decode_mp3_file(dir.path() / "empty.mp3", audio) == Mp3Error::Io);
}

TEST_CASE("music discovery finds only mp3 files", "[mp3]") {
    TempDir dir;
    dir.write("Sounds/2.mp3", "x");
    dir.write("Sounds/3.MP3", "x");      // case should not matter
    dir.write("Sounds/Audio.snd", "x");  // not music
    dir.write("Sounds/notes.txt", "x");

    const auto tracks = find_music(dir.path());
    REQUIRE(tracks.size() == 2);
    REQUIRE(tracks[0].filename() == "2.mp3");
    REQUIRE(tracks[1].filename() == "3.MP3");
}

TEST_CASE("numeric track names sort numerically, not as text", "[mp3]") {
    // MM6 names its tracks 2..16, which sorted as text puts 10 before 2.
    TempDir dir;
    for (const char* n : {"2", "3", "9", "10", "11", "16"}) {
        dir.write(std::string("Sounds/") + n + ".mp3", "x");
    }
    const auto tracks = find_music(dir.path());
    REQUIRE(tracks.size() == 6);
    REQUIRE(tracks[0].stem() == "2");
    REQUIRE(tracks[1].stem() == "3");
    REQUIRE(tracks[2].stem() == "9");
    REQUIRE(tracks[3].stem() == "10");
    REQUIRE(tracks[5].stem() == "16");
}

TEST_CASE("a missing Sounds directory yields no tracks", "[mp3]") {
    TempDir dir;
    REQUIRE(find_music(dir.path()).empty());
    REQUIRE(find_music("/nonexistent/starhaven").empty());
}
