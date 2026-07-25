// Tests for the MM6 .LOD archive reader.
//
// Every fixture here is SYNTHETIC: it is hand-built from the format
// specification in docs/formats/lod.md and contains no bytes from the game.
// Compatibility checks against the user's real install live in a separate,
// opt-in, non-expressive test that is not part of this hermetic suite.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "core/lod/lod_archive.hpp"

using namespace starhaven::lod;

namespace {

// Verified layout constants (mirror src/core/lod/lod_archive.cpp).
constexpr std::uint32_t kHeaderSize = 288;
constexpr std::uint32_t kLodTypeOffset = 0x100;
constexpr std::uint32_t kArchiveStartOffset = 0x110;
constexpr std::uint32_t kCountOffset = 0x11C;
constexpr std::uint32_t kEntrySize = 32;

// Ensure the buffer is large enough for a write of `width` bytes at `off`,
// zero-extending it. Fixtures build a buffer incrementally (header then
// directory records then payloads), so growth is unavoidable here.
void ensure_size(std::vector<std::byte>& v, std::size_t needed) {
    if (v.size() < needed) {
        v.resize(needed, std::byte{0});
    }
}

void put_u16_le(std::vector<std::byte>& v, std::size_t off, std::uint16_t x) {
    ensure_size(v, off + 2);
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
}
void put_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    ensure_size(v, off + 4);
    v[off]     = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}
void put_string(std::vector<std::byte>& v, std::size_t off,
                std::size_t width, const std::string& s) {
    ensure_size(v, off + width);
    std::fill(v.begin() + off, v.begin() + off + width, std::byte{0});
    const std::size_t n = std::min(s.size(), width);
    for (std::size_t i = 0; i < n; ++i) {
        v[off + i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
    }
}

// Build a header for a "bitmaps" archive with the given entry count. The buffer
// is sized to include the directory region (archive_start + count*kEntrySize);
// callers then write entries into that region and append payloads after it.
void write_header(std::vector<std::byte>& buf, std::uint16_t count) {
    const std::size_t dir_end = kHeaderSize + static_cast<std::size_t>(count) * kEntrySize;
    buf.assign(dir_end, std::byte{0});
    put_string(buf, 0, 4, "LOD\0");
    put_string(buf, 4, 4, "MMVI");      // version string starts at offset 4
    put_string(buf, kLodTypeOffset, 8, "bitmaps");
    put_u32_le(buf, kArchiveStartOffset, kHeaderSize);
    put_u16_le(buf, kCountOffset, count);
}

// Write one directory entry at the given absolute offset. addr is relative to
// archive_start; unpacked is 0 for an uncompressed entry.
void write_entry(std::vector<std::byte>& buf, std::size_t entry_off,
                 const std::string& name, std::uint32_t addr,
                 std::uint32_t size_field, std::uint32_t unpacked) {
    put_string(buf, entry_off, 16, name);
    put_u32_le(buf, entry_off + 16, addr);
    put_u32_le(buf, entry_off + 20, size_field);
    put_u32_le(buf, entry_off + 24, unpacked);
}

// A minimal valid archive: one uncompressed entry "hello" with a 5-byte payload.
std::vector<std::byte> make_single_entry_archive() {
    std::vector<std::byte> buf;
    constexpr std::uint16_t kCount = 1;
    write_header(buf, kCount);

    // Directory at archive_start (288); entry 0 at offset 288.
    // Payload follows at 288 + 32 = 320. addr is relative to archive_start (288),
    // so addr = 32.
    constexpr std::uint32_t kEntry0Off = kHeaderSize;
    constexpr std::uint32_t kPayloadAddr = 32;  // 320 - 288
    constexpr std::uint32_t kPayloadSize = 5;
    write_entry(buf, kEntry0Off, "hello", kPayloadAddr, kPayloadSize, 0);

    // Append the payload bytes.
    const std::string payload = "world";
    buf.insert(buf.end(),
               reinterpret_cast<const std::byte*>(payload.data()),
               reinterpret_cast<const std::byte*>(payload.data()) + payload.size());
    return buf;
}

}  // namespace

TEST_CASE("single uncompressed entry is enumerated", "[lod]") {
    LodArchive a;
    REQUIRE(LodArchive::parse(make_single_entry_archive(), a) == LodError::None);

    REQUIRE(a.kind() == LodKind::Bitmaps);
    REQUIRE(a.version() == "MMVI");
    REQUIRE(a.lod_type() == "bitmaps");
    REQUIRE(a.count() == 1);
    REQUIRE(a.entries().size() == 1);

    const auto& e = a.entries().front();
    REQUIRE(e.name == "hello");
    REQUIRE(e.uncompressed);
    REQUIRE(e.stored_size == 5);
}

TEST_CASE("payload returns uncompressed bytes", "[lod]") {
    LodArchive a;
    REQUIRE(LodArchive::parse(make_single_entry_archive(), a) == LodError::None);

    std::span<const std::byte> out;
    REQUIRE(a.payload("hello", out) == LodArchive::PayloadError::None);
    REQUIRE(out.size() == 5);
    REQUIRE(std::memcmp(out.data(), "world", 5) == 0);
}

TEST_CASE("find is case-insensitive", "[lod]") {
    LodArchive a;
    REQUIRE(LodArchive::parse(make_single_entry_archive(), a) == LodError::None);
    REQUIRE(a.find("HELLO").has_value());
    REQUIRE_FALSE(a.find("missing").has_value());
}

TEST_CASE("compressed entry is enumerated but payload is unsupported", "[lod]") {
    std::vector<std::byte> buf;
    write_header(buf, 1);
    write_entry(buf, kHeaderSize, "packed", /*addr=*/32, /*size_field=*/10, /*unpacked=*/100);
    // Payload bytes (content irrelevant; must be present so stored size fits).
    buf.insert(buf.end(), 10, std::byte{0xAA});

    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::None);
    REQUIRE(a.count() == 1);
    REQUIRE_FALSE(a.entries().front().uncompressed);

    std::span<const std::byte> out;
    REQUIRE(a.payload("packed", out) == LodArchive::PayloadError::Compressed);
}

TEST_CASE("size_field is the authoritative stored size", "[lod]") {
    // Two entries stored out of directory order with explicit size_fields. The
    // reader must trust size_field, not a gap to the next entry, because real
    // archives are not packed in table order (verified on BITMAPS.LOD).
    std::vector<std::byte> buf;
    write_header(buf, 2);
    // Directory for 2 entries occupies archive_start..archive_start+64. Put the
    // payloads right after it: entry "first" at relative addr 64, size 8;
    // entry "second" at relative addr 72, size 4.
    write_entry(buf, kHeaderSize,              "first",  /*addr=*/64, /*size_field=*/8, /*unpacked=*/0);
    write_entry(buf, kHeaderSize + kEntrySize, "second", /*addr=*/72, /*size_field=*/4, /*unpacked=*/0);
    // Append enough payload bytes to cover both (8 + 4 = 12).
    buf.insert(buf.end(), 12, std::byte{0});

    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::None);
    REQUIRE(a.entries().size() == 2);
    REQUIRE(a.entries()[0].stored_size == 8);
    REQUIRE(a.entries()[1].stored_size == 4);
}

TEST_CASE("size_field that runs past EOF is rejected", "[lod]") {
    std::vector<std::byte> buf;
    write_header(buf, 1);
    // Claim a size_field larger than the bytes that follow the directory.
    write_entry(buf, kHeaderSize, "big", /*addr=*/32, /*size_field=*/1000, /*unpacked=*/0);
    buf.insert(buf.end(), 4, std::byte{0});  // only 4 bytes actually present

    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::EntrySizeOutOfRange);
}

TEST_CASE("truncated header is rejected", "[lod]") {
    std::vector<std::byte> buf(kHeaderSize - 1, std::byte{0});
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::TooSmall);
}

TEST_CASE("bad magic is rejected", "[lod]") {
    auto buf = make_single_entry_archive();
    put_string(buf, 0, 4, "XXXX");
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::BadMagic);
}

TEST_CASE("Heroes III version code is unsupported", "[lod]") {
    auto buf = make_single_entry_archive();
    // Version code at offset 4 as a u32 <= 0xFFFF -> Heroes III.
    put_u32_le(buf, 4, 0x00001000);
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::UnsupportedVersion);
}

TEST_CASE("unknown lod_type is rejected", "[lod]") {
    auto buf = make_single_entry_archive();
    put_string(buf, kLodTypeOffset, 8, "bogus");
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::UnknownLodType);
}

TEST_CASE("count overflowing the directory table is rejected", "[lod]") {
    auto buf = make_single_entry_archive();
    // Claim an absurd entry count that cannot fit.
    put_u16_le(buf, kCountOffset, 65535);
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::CountTooLarge);
}

TEST_CASE("entry whose data offset is past EOF is rejected", "[lod]") {
    auto buf = make_single_entry_archive();
    // Point entry 0 at an address far beyond the file end.
    put_u32_le(buf, kHeaderSize + 16, 0x40000000);
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::EntryOffsetOutOfRange);
}

TEST_CASE("game lod_type is reported as unsupported variant", "[lod]") {
    // Games.lod uses a different directory layout; the standard parser must not
    // misread it. We only need the header to carry lod_type "game".
    std::vector<std::byte> buf;
    write_header(buf, 0);
    put_string(buf, kLodTypeOffset, 5, "game");
    LodArchive a;
    REQUIRE(LodArchive::parse(buf, a) == LodError::UnknownLodType);
}
