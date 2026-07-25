// Tests for the DTILE.BIN ground-tile table parser.
//
// Hermetic: every fixture is synthesized from the format described in
// docs/formats/dtile.md. No bytes are copied from a game archive.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include "core/world/tile_table.hpp"

using namespace starhaven::world;

namespace {

struct Row {
    std::string name;
    std::uint16_t group, section, attrs;
};

void put_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}

// Build the decompressed table body: u32 count + count * 26-byte records.
std::vector<std::uint8_t> make_body(const std::vector<Row>& rows) {
    std::vector<std::uint8_t> v;
    const std::uint32_t n = static_cast<std::uint32_t>(rows.size());
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    for (const Row& r : rows) {
        for (std::size_t k = 0; k < 16; ++k) {
            v.push_back(k < r.name.size() ? static_cast<std::uint8_t>(r.name[k]) : 0);
        }
        put_u16(v, 0);
        put_u16(v, 0);
        put_u16(v, r.group);
        put_u16(v, r.section);
        put_u16(v, r.attrs);
    }
    return v;
}

// Wrap a body as a stored archive entry: 48-byte header + zlib stream.
std::vector<std::byte> make_entry(const std::vector<std::uint8_t>& body) {
    uLongf cap = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::uint8_t> z(cap);
    REQUIRE(compress(z.data(), &cap, body.data(), static_cast<uLong>(body.size())) == Z_OK);
    z.resize(cap);

    std::vector<std::byte> out(48, std::byte{0});
    const char* nm = "dtile.bin";
    std::memcpy(out.data(), nm, std::strlen(nm));
    for (std::uint8_t b : z)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

std::vector<std::byte> entry_of(const std::vector<Row>& rows) {
    return make_entry(make_body(rows));
}

}  // namespace

TEST_CASE("parses a well-formed table", "[tile_table]") {
    const auto e = entry_of({{"dirttyl", 4, 0, 0}, {"grastyl", 0, 1, 0}, {"grdrtNE", 0, 12, 512}});
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::None);
    REQUIRE(t.size() == 3);
    REQUIRE(t.entries()[0].name == "dirttyl");
    REQUIRE(t.entries()[0].group == 4);
    REQUIRE(t.entries()[2].name == "grdrtNE");
    REQUIRE(t.entries()[2].section == 12);
    REQUIRE(t.entries()[2].attributes == 512);
}

TEST_CASE("a tilemap byte indexes the table directly", "[tile_table]") {
    // The lookup is direct, not remapped through the map's tileset offsets.
    // See the refutation table in docs/formats/dtile.md.
    std::vector<Row> rows;
    for (int i = 0; i < 120; ++i)
        rows.push_back({"filler", 0, 0, 0});
    rows[90] = {"grastyl", 0, 0, 0};
    rows[1] = {"dirttyl", 4, 0, 0};
    const auto e = entry_of(rows);

    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::None);
    REQUIRE(t.at(90) != nullptr);
    REQUIRE(t.at(90)->name == "grastyl");
    REQUIRE(t.at(1)->name == "dirttyl");
}

TEST_CASE("an index past the end resolves to nullptr, not garbage", "[tile_table]") {
    const auto e = entry_of({{"dirttyl", 4, 0, 0}});
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::None);
    REQUIRE(t.at(0) != nullptr);
    // A tilemap byte can reach 255 while a truncated table holds one row.
    REQUIRE(t.at(1) == nullptr);
    REQUIRE(t.at(255) == nullptr);
}

TEST_CASE("an empty name is preserved as a reserved slot", "[tile_table]") {
    // The shipped table has more rows than art; empty rows must parse rather
    // than abort, so callers can skip them.
    const auto e = entry_of({{"", 0, 0, 0}, {"grastyl", 0, 0, 0}});
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::None);
    REQUIRE(t.entries()[0].name.empty());
    REQUIRE(t.entries()[1].name == "grastyl");
}

TEST_CASE("a name filling all 16 bytes does not run past its field", "[tile_table]") {
    const auto e = entry_of({{"0123456789abcdef", 1, 2, 3}});
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::None);
    REQUIRE(t.entries()[0].name == "0123456789abcdef");
    REQUIRE(t.entries()[0].group == 1);
}

TEST_CASE("rejects an entry shorter than the header", "[tile_table]") {
    std::vector<std::byte> tiny(20, std::byte{0});
    TileTable t;
    REQUIRE(TileTable::parse(tiny, t) == TileTableError::TooSmall);
    REQUIRE(t.size() == 0);
}

TEST_CASE("rejects a header with no valid zlib stream", "[tile_table]") {
    std::vector<std::byte> junk(120, std::byte{0x41});
    TileTable t;
    REQUIRE(TileTable::parse(junk, t) == TileTableError::NotCompressed);
    REQUIRE(t.size() == 0);
}

TEST_CASE("rejects a count that disagrees with the payload length", "[tile_table]") {
    // Claim four records but supply two. Truncating instead would leave a
    // half-populated table that silently renders the wrong ground.
    auto body = make_body({{"a", 0, 0, 0}, {"b", 0, 0, 0}});
    body[0] = 4;
    const auto e = make_entry(body);
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::BadCount);
    REQUIRE(t.size() == 0);
}

TEST_CASE("rejects a body too short to hold the count", "[tile_table]") {
    const auto e = make_entry({0x01, 0x02});
    TileTable t;
    REQUIRE(TileTable::parse(e, t) == TileTableError::BadCount);
}
