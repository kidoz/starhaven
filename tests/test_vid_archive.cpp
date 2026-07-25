// Tests for the .vid video container reader.
//
// Fixtures are SYNTHETIC: directories and payloads are built here from the
// layout in docs/formats/vid.md. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/video/vid_archive.hpp"

using namespace openmm6::video;

namespace {

void put_u32(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off + 0] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

// Build an archive holding `payloads`, named "vid0", "vid1", ... Each payload
// is filled with a byte derived from its index so reads can be checked.
std::vector<std::byte> make_archive(const std::vector<std::size_t>& payloads) {
    const std::size_t count = payloads.size();
    const std::size_t dir_end = 4 + count * kVidEntrySize;
    std::size_t total = dir_end;
    for (std::size_t n : payloads) {
        total += n;
    }

    std::vector<std::byte> data(total, std::byte{0});
    put_u32(data, 0, static_cast<std::uint32_t>(count));

    std::size_t offset = dir_end;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t entry = 4 + i * kVidEntrySize;
        const std::string name = "vid" + std::to_string(i);
        for (std::size_t k = 0; k < name.size(); ++k) {
            data[entry + k] = static_cast<std::byte>(name[k]);
        }
        put_u32(data, entry + kVidNameSize, static_cast<std::uint32_t>(offset));
        for (std::size_t k = 0; k < payloads[i]; ++k) {
            data[offset + k] = static_cast<std::byte>(0xA0 + i);
        }
        offset += payloads[i];
    }
    return data;
}

}  // namespace

TEST_CASE("a directory is parsed and sizes come from the gaps", "[vid]") {
    auto data = make_archive({16, 32, 8});
    VidArchive archive;
    REQUIRE(VidArchive::parse(std::move(data), archive) == VidError::None);
    REQUIRE(archive.size() == 3);

    REQUIRE(archive.entries()[0].name == "vid0");
    REQUIRE(archive.entries()[1].name == "vid1");
    REQUIRE(archive.entries()[2].name == "vid2");

    // The directory stores only offsets; sizes are derived, and the last entry
    // runs to the end of the file.
    REQUIRE(archive.entries()[0].size == 16);
    REQUIRE(archive.entries()[1].size == 32);
    REQUIRE(archive.entries()[2].size == 8);
    REQUIRE(archive.entries()[0].offset == 4 + 3 * kVidEntrySize);
}

TEST_CASE("entries are found case-insensitively", "[vid]") {
    auto data = make_archive({4, 4});
    VidArchive archive;
    REQUIRE(VidArchive::parse(std::move(data), archive) == VidError::None);
    REQUIRE(archive.find("vid1") == 1);
    REQUIRE(archive.find("VID1") == 1);
    REQUIRE(archive.find("ViD1") == 1);
    REQUIRE(archive.find("absent") == archive.size());
}

TEST_CASE("reading an entry returns exactly its bytes", "[vid]") {
    auto data = make_archive({5, 7});
    VidArchive archive;
    REQUIRE(VidArchive::parse(std::move(data), archive) == VidError::None);

    std::vector<std::byte> out;
    REQUIRE(archive.read(1, out));
    REQUIRE(out.size() == 7);
    for (std::byte b : out) {
        REQUIRE(b == std::byte{0xA1});
    }
    REQUIRE_FALSE(archive.read(2, out));  // out of range
}

TEST_CASE("a buffer too small for the count is rejected", "[vid]") {
    VidArchive archive;
    std::vector<std::byte> tiny(2, std::byte{0});
    REQUIRE(VidArchive::parse(std::move(tiny), archive) == VidError::TooSmall);
}

TEST_CASE("a zero or oversized entry count is rejected", "[vid]") {
    VidArchive archive;

    std::vector<std::byte> zero(64, std::byte{0});
    put_u32(zero, 0, 0);
    REQUIRE(VidArchive::parse(std::move(zero), archive) == VidError::BadCount);

    // A count whose directory cannot fit in the buffer.
    std::vector<std::byte> huge(64, std::byte{0});
    put_u32(huge, 0, 1000);
    REQUIRE(VidArchive::parse(std::move(huge), archive) == VidError::BadCount);
}

TEST_CASE("an offset outside the file is rejected", "[vid]") {
    VidArchive archive;

    auto past_end = make_archive({8});
    put_u32(past_end, 4 + kVidNameSize, 0xFFFF);
    REQUIRE(VidArchive::parse(std::move(past_end), archive) == VidError::BadOffset);

    // An offset inside the directory would make the entry overlap it.
    auto into_dir = make_archive({8});
    put_u32(into_dir, 4 + kVidNameSize, 8);
    REQUIRE(VidArchive::parse(std::move(into_dir), archive) == VidError::BadOffset);
}

TEST_CASE("descending offsets are rejected rather than yielding huge sizes",
          "[vid]") {
    // Sizes are derived from the next entry's offset, so out-of-order offsets
    // would otherwise underflow into an enormous size.
    auto data = make_archive({16, 16});
    const std::uint32_t first = 4 + 2 * kVidEntrySize;
    put_u32(data, 4 + kVidNameSize, first + 16);
    put_u32(data, 4 + kVidEntrySize + kVidNameSize, first);

    VidArchive archive;
    REQUIRE(VidArchive::parse(std::move(data), archive) == VidError::BadOffset);
}

TEST_CASE("a name filling the whole field does not run past it", "[vid]") {
    auto data = make_archive({4});
    for (std::size_t i = 0; i < kVidNameSize; ++i) {
        data[4 + i] = static_cast<std::byte>('x');
    }
    VidArchive archive;
    REQUIRE(VidArchive::parse(std::move(data), archive) == VidError::None);
    REQUIRE(archive.entries()[0].name == std::string(kVidNameSize, 'x'));
}
