// Tests for the BLV room/sector parser (`L.RData` + `L.RLData`).
//
// Fixtures are SYNTHETIC: a sector region is assembled here from the layout in
// docs/formats/blv.md. No bytes from the game are involved.
//
// The pure core `carve_sectors` is exercised directly, so the fixtures build a
// sector region without a full decoration block.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/world/blv_map.hpp"

using namespace starhaven::world;

namespace {

void put_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    v[off + 0] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::uint8_t>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::uint8_t>((x >> 24) & 0xFF);
}

void put_i16(std::vector<std::uint8_t>& v, std::size_t off, std::int16_t x) {
    v[off + 0] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(x) & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(x) >> 8) & 0xFF);
}

void put_u16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    v[off + 0] = static_cast<std::uint8_t>(x & 0xFF);
    v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
}

struct SectorSpec {
    // The eight slot counts, in walk order. Slot 2 (index 1) is the face list.
    std::array<std::int16_t, 8> slots{};
    std::int16_t xmin = -100, xmax = 100;
    std::int16_t ymin = -100, ymax = 100;
    std::int16_t zmin = 0, zmax = 200;
    // The face ids written into slot 2's RLData sub-range. Its length must
    // equal slots[1].
    std::vector<std::uint16_t> face_ids;
};

// Build `[start, stop)` of `payload` as a sector region: a u32 sector count,
// then the 116-byte records, then the flat u16 RLData pool the records' slots
// index into. The pool is laid out by walking each sector's eight counts in
// order; slot 2's `face_ids` fill its sub-range.
std::vector<std::uint8_t> make_region(const std::vector<SectorSpec>& sectors) {
    const std::size_t records_bytes = sectors.size() * kBlvSectorSize;
    std::size_t pool_entries = 0;
    for (const auto& s : sectors) {
        for (std::int16_t c : s.slots) {
            pool_entries += static_cast<std::size_t>(c);
        }
    }
    std::vector<std::uint8_t> p(4 + records_bytes + pool_entries * 2, 0);
    put_u32(p, 0, static_cast<std::uint32_t>(sectors.size()));

    // The pool begins after the records. Slot sub-ranges are carved by walking
    // the counts in order, so we mirror that here to lay out the face ids.
    std::size_t pool_at = 4 + records_bytes;
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        const std::size_t rec = 4 + i * kBlvSectorSize;
        const SectorSpec& s = sectors[i];
        for (std::size_t k = 0; k < kBlvSectorSlotCount; ++k) {
            put_i16(p, rec + kBlvSectorSlotOffsets[k], s.slots[k]);
        }
        put_i16(p, rec + kBlvSectorAabbOffset + 0, s.xmin);
        put_i16(p, rec + kBlvSectorAabbOffset + 2, s.xmax);
        put_i16(p, rec + kBlvSectorAabbOffset + 4, s.ymin);
        put_i16(p, rec + kBlvSectorAabbOffset + 6, s.ymax);
        put_i16(p, rec + kBlvSectorAabbOffset + 8, s.zmin);
        put_i16(p, rec + kBlvSectorAabbOffset + 10, s.zmax);

        std::size_t slot_pool = pool_at;
        for (std::size_t k = 0; k < kBlvSectorSlotCount; ++k) {
            if (k == kBlvSectorSlotFaces) {
                // Write this sector's face ids into slot 2's sub-range.
                REQUIRE(s.face_ids.size() == static_cast<std::size_t>(s.slots[k]));
                for (std::size_t f = 0; f < s.face_ids.size(); ++f) {
                    put_u16(p, slot_pool + f * 2, s.face_ids[f]);
                }
            }
            slot_pool += static_cast<std::size_t>(s.slots[k]) * 2;
        }
        pool_at = slot_pool;
    }
    return p;
}

}  // namespace

TEST_CASE("sectors carve their face lists out of the pool", "[blv_sector]") {
    // Two sectors, each owning two faces, with other slots populated to prove
    // the face sub-range is carved correctly (not read from the wrong slot).
    const std::vector<SectorSpec> specs{
        SectorSpec{.slots = {1, 2, 0, 3, 0, 0, 0, 0}, .face_ids = {0, 1}},
        SectorSpec{.slots = {0, 2, 1, 0, 0, 0, 0, 0}, .face_ids = {2, 3}},
    };
    const auto region = make_region(specs);

    std::vector<BlvSector> out;
    REQUIRE(carve_sectors(region, 0, region.size(), /*face_count=*/4, out));
    REQUIRE(out.size() == 2);

    REQUIRE(out[0].face_ids.size() == 2);
    REQUIRE(out[0].face_ids[0] == 0);
    REQUIRE(out[0].face_ids[1] == 1);
    REQUIRE(out[0].slot_counts[0] == 1);  // slot 1 (portals)
    REQUIRE(out[0].slot_counts[3] == 3);  // slot 4 (collision)
    REQUIRE(out[0].min_x == -100);
    REQUIRE(out[0].max_z == 200);

    REQUIRE(out[1].face_ids.size() == 2);
    REQUIRE(out[1].face_ids[0] == 2);
    REQUIRE(out[1].face_ids[1] == 3);
}

TEST_CASE("sectors reject malformed input deterministically", "[blv_sector]") {
    std::vector<BlvSector> out;

    // Too few bytes to hold even the count.
    REQUIRE_FALSE(carve_sectors(std::span<const std::uint8_t>{}, 0, 0, 4, out));

    // Truncated: count claims records that run past `stop`.
    std::vector<std::uint8_t> trunc(8, 0);
    put_u32(trunc, 0, 100);  // 100 sectors x 116 bytes won't fit in an 8-byte buffer
    REQUIRE_FALSE(carve_sectors(trunc, 0, trunc.size(), 4, out));

    // Out-of-range face id.
    std::vector<SectorSpec> bad{
        SectorSpec{.slots = {0, 1, 0, 0, 0, 0, 0, 0}, .face_ids = {99}},
    };
    const auto region = make_region(bad);
    REQUIRE_FALSE(carve_sectors(region, 0, region.size(), /*face_count=*/4, out));
    REQUIRE(out.empty());  // rejected wholesale, no partial result

    // A slot count that overruns the pool.
    std::vector<std::uint8_t> overrun(4 + kBlvSectorSize, 0);
    put_u32(overrun, 0, 1);
    put_i16(overrun, 4 + kBlvSectorSlotOffsets[0], 5000);  // slot 1 claims 5000 entries
    REQUIRE_FALSE(carve_sectors(overrun, 0, overrun.size(), 4, out));
}
