// Tests for the Games.lod container reader.
//
// Fixtures are SYNTHETIC: hand-built from docs/formats/games-lod.md. No bytes
// from the game are involved.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "core/lod/game_lod_archive.hpp"

using namespace openmm6::lod;

namespace {

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
    if (v.size() < off + width) {
        v.resize(off + width, std::byte{0});
    }
    std::fill(v.begin() + off, v.begin() + off + width, std::byte{0});
    const std::size_t n = std::min(s.size(), width);
    for (std::size_t i = 0; i < n; ++i) {
        v[off + i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
    }
}

constexpr std::uint32_t kHeaderSize = 256;
constexpr std::uint32_t kRootEntryOffset = 256;
constexpr std::uint32_t kFileEntrySize = 32;

// Build a header + root entry for a Games.lod with `count` file entries.
void write_header(std::vector<std::byte>& buf, std::uint16_t count,
                  std::uint32_t root_data_offset) {
    buf.assign(root_data_offset, std::byte{0});
    put_string(buf, 0x00, 4, "LOD\0");
    put_string(buf, 0x04, 8, "GameMMVI");
    put_string(buf, 0x54, 80, "Maps for MMVI");
    put_u32_le(buf, 0xA8, 0);            // unk_0
    put_u32_le(buf, 0xAC, 1);            // numDirectories
    // Root entry at 256.
    put_string(buf, kRootEntryOffset, 4, "maps");
    put_u32_le(buf, kRootEntryOffset + 0x10, root_data_offset);
    put_u32_le(buf, kRootEntryOffset + 0x14, 0);  // dataSize (not trusted)
    put_u16_le(buf, kRootEntryOffset + 0x1C, count);
}

// Write one file entry at the given absolute offset within the table.
void write_file_entry(std::vector<std::byte>& buf, std::size_t entry_off,
                      const std::string& name, std::uint32_t rel_off,
                      std::uint32_t size) {
    ensure_size(buf, entry_off + kFileEntrySize);  // materialize the full record
    put_string(buf, entry_off, 16, name);
    put_u32_le(buf, entry_off + 0x10, rel_off);
    put_u32_le(buf, entry_off + 0x14, size);
    put_u32_le(buf, entry_off + 0x18, 0);  // unk_0
    put_u16_le(buf, entry_off + 0x1C, 0);  // numItems (file)
}

// A minimal Games.lod: one entry "d01.blv" with a 4-byte payload.
std::vector<std::byte> make_single_entry_archive() {
    constexpr std::uint16_t kCount = 1;
    constexpr std::uint32_t kRootDataOff = kHeaderSize + 32 + kCount * kFileEntrySize; // 320
    std::vector<std::byte> buf;
    write_header(buf, kCount, kRootDataOff);

    // The file-entry table occupies rootDataOff .. rootDataOff + count*32.
    // Payload follows immediately after, so entry 0's rel offset is count*32.
    write_file_entry(buf, kRootDataOff, "d01.blv",
                     /*rel_off*/ kCount * kFileEntrySize, /*size*/ 4);
    // Materialize the full table region + payload so all offsets are valid.
    const std::size_t payload_off = kRootDataOff + kCount * kFileEntrySize;
    if (buf.size() < payload_off) {
        buf.resize(payload_off, std::byte{0});
    }
    const std::string payload = "MAP1";
    buf.insert(buf.end(),
               reinterpret_cast<const std::byte*>(payload.data()),
               reinterpret_cast<const std::byte*>(payload.data()) + payload.size());
    return buf;
}

}  // namespace

TEST_CASE("single entry is enumerated", "[game_lod]") {
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(make_single_entry_archive(), a) == GameLodError::None);
    REQUIRE(a.version() == "GameMMVI");
    REQUIRE(a.description() == "Maps for MMVI");
    REQUIRE(a.root_name() == "maps");
    REQUIRE(a.num_items() == 1);
    REQUIRE(a.entries().size() == 1);

    const auto& e = a.entries().front();
    REQUIRE(e.name == "d01.blv");
    REQUIRE(e.data_size == 4);
}

TEST_CASE("payload returns raw entry bytes", "[game_lod]") {
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(make_single_entry_archive(), a) == GameLodError::None);

    std::span<const std::byte> out;
    REQUIRE(a.payload("d01.blv", out) == GameLodArchive::PayloadError::None);
    REQUIRE(out.size() == 4);
    REQUIRE(std::memcmp(out.data(), "MAP1", 4) == 0);
}

TEST_CASE("find is case-insensitive", "[game_lod]") {
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(make_single_entry_archive(), a) == GameLodError::None);
    REQUIRE(a.find("D01.BLV").has_value());
    REQUIRE_FALSE(a.find("missing.blv").has_value());
}

TEST_CASE("truncated header is rejected", "[game_lod]") {
    std::vector<std::byte> buf(100, std::byte{0});
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::TooSmall);
}

TEST_CASE("bad signature is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    put_string(buf, 0x00, 4, "XXXX");
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::BadSignature);
}

TEST_CASE("non-Game version is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    put_string(buf, 0x04, 4, "MMVI");  // standard archive version, not Game*
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::UnsupportedVersion);
}

TEST_CASE("numDirectories other than 1 is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    put_u32_le(buf, 0xAC, 2);
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::UnsupportedDirectoryCount);
}

TEST_CASE("bad root data offset is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    // Point the root data offset before the header+root entry.
    put_u32_le(buf, kRootEntryOffset + 0x10, 10);
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::BadRootOffset);
}

TEST_CASE("numItems overflowing the table is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    put_u16_le(buf, kRootEntryOffset + 0x1C, 65535);
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::TableTooLarge);
}

TEST_CASE("entry data past the file is rejected", "[game_lod]") {
    auto buf = make_single_entry_archive();
    // Lie about entry 0's size so it runs past EOF. The file entry's size field
    // is at rootDataOffset + 0x14 (rootDataOffset == start of the entry table).
    const std::uint32_t root_data_off = kHeaderSize + 32 + 1 * kFileEntrySize;
    put_u32_le(buf, root_data_off + 0x14, 0x40000000);
    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::EntryOutOfRange);
}

TEST_CASE("multiple entries enumerate in order", "[game_lod]") {
    constexpr std::uint16_t kCount = 2;
    constexpr std::uint32_t kRootDataOff = kHeaderSize + 32 + kCount * kFileEntrySize;
    std::vector<std::byte> buf;
    write_header(buf, kCount, kRootDataOff);
    // Two file entries; payloads follow the table at rel offset count*32.
    write_file_entry(buf, kRootDataOff + 0 * kFileEntrySize, "a.blv", 2 * kFileEntrySize, 2);
    write_file_entry(buf, kRootDataOff + 1 * kFileEntrySize, "b.blv", 2 * kFileEntrySize + 2, 3);
    // Materialize the table region, then append the 5 payload bytes.
    const std::size_t payload_off = kRootDataOff + kCount * kFileEntrySize;
    if (buf.size() < payload_off) {
        buf.resize(payload_off, std::byte{0});
    }
    buf.insert(buf.end(), 5, std::byte{0});

    GameLodArchive a;
    REQUIRE(GameLodArchive::parse(buf, a) == GameLodError::None);
    REQUIRE(a.entries().size() == 2);
    REQUIRE(a.entries()[0].name == "a.blv");
    REQUIRE(a.entries()[1].name == "b.blv");
}
