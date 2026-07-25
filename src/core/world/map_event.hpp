#ifndef STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
#define STARHAVEN_CORE_WORLD_MAP_EVENT_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// A decompressed map event-data file (.ddm outdoor / .dlv indoor). The payload
// is a large fixed-size structure holding the map's event tables; its internal
// layout is decoded in a later slice. This type exposes the decompressed bytes.
struct MapEventFile {
    std::vector<std::uint8_t> payload;
};

// Outcome of parsing. Callers convert these into user-facing text; the parser
// never throws.
enum class MapEventError {
    None,
    // Input too small for the 8-byte wrapper.
    TooSmall,
    // zlib inflate failed.
    InflateFailed,
    // The inflated length does not match the wrapper's decompressedSize.
    SizeMismatch,
};

// Parse a raw .ddm/.dlv entry (as read from Games.lod) into a MapEventFile.
// Uses the same 8-byte zlib wrapper as the .odm parser (see
// docs/formats/event-data.md).
[[nodiscard]] MapEventError parse_map_event(std::span<const std::byte> entry,
                                            MapEventFile& out);

// Fixed wrapper size, exposed for tests and tools.
constexpr std::uint32_t kEventWrapperSize = 8;

// --- First event table (see docs/formats/event-tables.md) -----------------

// One populated record from the first event table. Only the verified leading
// fields are exposed; the 548-byte body is decoded in a later slice.
struct EventTableRecord {
    std::int32_t type = 0;   // record type (e.g. 20); 0 = empty slot
    std::string name;        // NUL-padded ASCII at +4
};

// Verified layout constants for the first event table.
constexpr std::uint32_t kEventTableOffset = 0x798;
constexpr std::uint32_t kEventRecordSize = 548;     // 0x224
constexpr std::uint32_t kEventRecordNameOffset = 4;
constexpr std::uint32_t kEventRecordNameMax = 12;   // verified readable prefix

// Enumerate the populated records of the first event table. A record counts as
// populated if its type is nonzero OR its name field has any nonzero byte.
// Stops at the end of the payload or after `max_records` (whichever comes
// first); empty trailing slots are skipped.
[[nodiscard]] std::vector<EventTableRecord>
enumerate_event_table(const MapEventFile& file, std::size_t max_records = 4096);

// --- Actors (see docs/formats/event-actors.md) -----------------------------

// One placed actor: a named monster or NPC standing somewhere on the map.
//
// The 548-byte record holds far more than this — stats and behaviour flags are
// visible but unconfirmed — so only the fields verified against the maps
// themselves are exposed.
struct MapActor {
    std::string name;                   // e.g. "Peasant"
    std::int16_t x = 0, y = 0, z = 0;   // world position, MM6 axes
};

// Extract the actor array: the same 548-byte records `enumerate_event_table`
// walks, read for their name and position.
//
// The array has no count field, so it is read until the first slot whose name
// is not plausible text. That rule is what keeps a trailing garbage slot — one
// exists in several shipped files — out of the result.
[[nodiscard]] std::vector<MapActor>
extract_actors(const MapEventFile& file, std::size_t max_records = 4096);

// Offset of the position triple within a record, and the minimum name length
// treated as a real entry.
constexpr std::uint32_t kActorPositionOffset = 0x82;
constexpr std::size_t kActorMinNameLength = 2;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_EVENT_HPP
