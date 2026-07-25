// Tests for the .snd sound-archive reader.
//
// Fixtures are SYNTHETIC: directories and payloads are built here from the
// layout in docs/formats/snd.md. No bytes from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <zlib.h>

#include "core/audio/snd_archive.hpp"

using namespace starhaven::audio;

namespace {

void put_u32(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v[off + static_cast<std::size_t>(i)] =
            static_cast<std::byte>((x >> (8*i)) & 0xFF);
    }
}

// Build an archive from (name, payload, compress) triples.
std::vector<std::byte> make_archive(
    const std::vector<std::tuple<std::string, std::vector<std::uint8_t>, bool>>& items) {
    const std::size_t count = items.size();
    const std::size_t dir_end = 4 + count * kSndEntrySize;

    std::vector<std::vector<std::uint8_t>> stored;
    for (const auto& [name, payload, compress] : items) {
        (void)name;
        if (!compress) {
            stored.push_back(payload);
            continue;
        }
        uLongf bound = compressBound(static_cast<uLong>(payload.size()));
        std::vector<std::uint8_t> out(bound);
        uLongf len = bound;
        REQUIRE(compress2(out.data(), &len, payload.data(),
                          static_cast<uLong>(payload.size()),
                          Z_DEFAULT_COMPRESSION) == Z_OK);
        out.resize(len);
        stored.push_back(std::move(out));
    }

    std::size_t total = dir_end;
    for (const auto& s : stored) total += s.size();

    std::vector<std::byte> data(total, std::byte{0});
    put_u32(data, 0, static_cast<std::uint32_t>(count));

    std::size_t offset = dir_end;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& [name, payload, compress] = items[i];
        (void)compress;
        const std::size_t entry = 4 + i * kSndEntrySize;
        for (std::size_t k = 0; k < name.size() && k < kSndNameSize - 1; ++k) {
            data[entry + k] = static_cast<std::byte>(name[k]);
        }
        put_u32(data, entry + kSndNameSize, static_cast<std::uint32_t>(offset));
        put_u32(data, entry + kSndNameSize + 4,
                static_cast<std::uint32_t>(stored[i].size()));
        put_u32(data, entry + kSndNameSize + 8,
                static_cast<std::uint32_t>(payload.size()));
        for (std::size_t k = 0; k < stored[i].size(); ++k) {
            data[offset + k] = static_cast<std::byte>(stored[i][k]);
        }
        offset += stored[i].size();
    }
    return data;
}

const std::vector<std::uint8_t> kPayloadA = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
const std::vector<std::uint8_t> kPayloadB(64, 0x5A);

}  // namespace

TEST_CASE("a directory is parsed", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}, {"second", kPayloadB, true}});
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    REQUIRE(archive.size() == 2);
    REQUIRE(archive.entries()[0].name == "first");
    REQUIRE(archive.entries()[1].name == "second");
    REQUIRE(archive.entries()[0].unpacked_size == kPayloadA.size());
}

TEST_CASE("a compressed entry inflates to its payload", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}});
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    std::vector<std::uint8_t> out;
    REQUIRE(archive.read(0, out) == SndError::None);
    REQUIRE(out == kPayloadA);
}

TEST_CASE("an entry stored uncompressed is returned as-is", "[snd]") {
    // One of MM6's 1,526 entries is stored this way; treating it as zlib fails.
    auto data = make_archive({{"raw", kPayloadA, false}});
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    REQUIRE(archive.entries()[0].stored());
    std::vector<std::uint8_t> out;
    REQUIRE(archive.read(0, out) == SndError::None);
    REQUIRE(out == kPayloadA);
}

TEST_CASE("entries are found case-insensitively", "[snd]") {
    auto data = make_archive({{"Sword_Hit", kPayloadA, true}});
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    REQUIRE(archive.find("sword_hit") == 0);
    REQUIRE(archive.find("SWORD_HIT") == 0);
    REQUIRE(archive.find("absent") == archive.size());
}

TEST_CASE("a buffer too small for the count is rejected", "[snd]") {
    SndArchive archive;
    std::vector<std::byte> tiny(2, std::byte{0});
    REQUIRE(SndArchive::parse(std::move(tiny), archive) == SndError::TooSmall);
}

TEST_CASE("a zero or oversized count is rejected", "[snd]") {
    SndArchive archive;
    std::vector<std::byte> zero(128, std::byte{0});
    REQUIRE(SndArchive::parse(std::move(zero), archive) == SndError::BadCount);

    std::vector<std::byte> huge(128, std::byte{0});
    put_u32(huge, 0, 5000);   // a directory far larger than the buffer
    REQUIRE(SndArchive::parse(std::move(huge), archive) == SndError::BadCount);
}

TEST_CASE("an entry pointing outside the file is rejected", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}});
    put_u32(data, 4 + kSndNameSize, 0xFFFFFF);   // offset past the end
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::BadOffset);
}

TEST_CASE("an entry overlapping the directory is rejected", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}});
    put_u32(data, 4 + kSndNameSize, 8);   // inside the directory
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::BadOffset);
}

TEST_CASE("corrupt compressed bytes are reported", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}});
    const std::size_t payload_at = 4 + kSndEntrySize;
    for (std::size_t i = payload_at; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(0xAA);
    }
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    std::vector<std::uint8_t> out;
    REQUIRE(archive.read(0, out) == SndError::InflateFailed);
}

TEST_CASE("reading past the last entry is rejected", "[snd]") {
    auto data = make_archive({{"first", kPayloadA, true}});
    SndArchive archive;
    REQUIRE(SndArchive::parse(std::move(data), archive) == SndError::None);
    std::vector<std::uint8_t> out;
    REQUIRE(archive.read(1, out) == SndError::BadOffset);
}
