#ifndef STARHAVEN_CORE_WORLD_MAP_SCRIPT_HPP
#define STARHAVEN_CORE_WORLD_MAP_SCRIPT_HPP

// A map's event script and its strings.
//
// Every map has a `.EVT` and a `.STR` entry in `icons.lod` — not in
// `Games.lod` with the geometry. The `.EVT` is a flat run of size-prefixed
// records grouped by event id; the `.STR` is the strings those records refer
// to. See docs/formats/map-events.md.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::world {

// One step of one event.
struct ScriptStep {
    std::uint16_t event_id = 0;
    std::uint8_t sequence = 0;  // counts from zero within the event
    std::uint8_t opcode = 0;
    std::vector<std::uint8_t> arguments;
};

// The three opcodes whose argument is known to index the string table. Each
// was identified by its arguments never leaving the map's own string count
// across all 83 scripts, and confirmed by what the strings say. See
// docs/formats/map-events.md.
inline constexpr std::uint8_t kOpcodeMessage = 29;      // "The door is locked."
inline constexpr std::uint8_t kOpcodeLongMessage = 30;  // a sign's full text
inline constexpr std::uint8_t kOpcodeName = 35;         // "Door", "Sign", "Chest"
inline constexpr std::uint8_t kOpcodeTitle = 5;         // what this place is called

// Enter an establishment. The argument is a `u32` `2DEvents.txt` row id: 474
// of the 504 distinct values across the fifteen outdoor maps are ids of a
// building on that very map.
inline constexpr std::uint8_t kOpcodeEnter = 2;

// Open a chest. The argument is an index into the event file's fixed 20-slot
// chest array: the largest value across all 65 scripts is 19.
inline constexpr std::uint8_t kOpcodeChest = 7;

// Move the party. The argument is a spawn point — X, Y, Z and a facing in
// MM6's own units — and a destination map file name, or `"0"` to stay on the
// map it is on: a teleporter rather than a door out. Across all scripts, 97 of
// the 99 named destinations are maps the design table lists, and the pairs are
// symmetric — GoblinWatch's exit names New Sorpigal's map and New Sorpigal
// names GoblinWatch's. Reproduce with `evt_info --transitions`.
inline constexpr std::uint8_t kOpcodeTravel = 6;

// The conditional machinery, named by shape and confirmed by whole events —
// see docs/formats/map-events.md, and reproduce with `evt_info --variables`.
//
// A check is `[type u8][value u32][step u8]` and jumps to the step when it
// passes: its trailing byte is a step of its own event on 1,951 of 1,951
// uses. Give, take and set are `[type u8][value u32]`. The end opcode closes
// every event; the goto's byte is a step on 368 of 368 uses.
// An event's opening step, present on 2,182 of 3,332 events, always first.
// Its argument is what the thing calls itself: an index into the map's own
// `.STR`, naming a non-empty string on 1,523 of 1,542 events that do not
// enter an establishment — and the strings are the interactable nouns,
// "Door" 419 times, "Chest" 244, "Lever", "Switch", "Drink from Fountain".
// On the events that do enter one, the header is the `2DEvents.txt` row
// instead, equal to the enter opcode's argument on 620 of 633. Reproduce
// with `evt_info --headers`.
inline constexpr std::uint8_t kOpcodeHeader = 4;

// Re-texture a face: `[face u32][texture name, NUL-terminated]`. All 215
// named uses point at `BITMAPS.LOD` entries, and the vocabulary is state —
// switches down (`t1swdu`), things on (`T3S1ON`), lava, night skies — so a
// thrown lever is drawn thrown. Reproduce with `evt_info --textures`.
inline constexpr std::uint8_t kOpcodeRetexture = 11;

inline constexpr std::uint8_t kOpcodeEnd = 1;
inline constexpr std::uint8_t kOpcodeCheck = 14;
inline constexpr std::uint8_t kOpcodeDoor = 15;  // `[door u8][open/shut u8]`
inline constexpr std::uint8_t kOpcodeGive = 16;
inline constexpr std::uint8_t kOpcodeTake = 17;
inline constexpr std::uint8_t kOpcodeSet = 18;
inline constexpr std::uint8_t kOpcodeGoto = 36;

// The quest chain's NPC rewrites. Setting a topic is `[npc u32][slot u8]
// [topic u32]` — 132 of 132 uses name an NPC row, the slot is 0..2 like the
// NPC table's three topic columns, and the topic resolves or is zero, which
// clears. Moving is `[npc u32][place u32]`, the place a `2DEvents.txt` row
// or zero for away — 29 of 29 and 29 of 29. Reproduce with
// `evt_info --npc-mutations`.
// Summon monsters: `[slot u8][variant u8][count u8][x i32][y i32][z i32]`.
// The slot is the map's own encounter slot — within its filled slots on 272
// of 272 resolvable uses — the variant is 1..3 like the monster table's
// A/B/C triples on 284 of 284, and the count runs 1..6. Reproduce with
// `evt_info --catalog 19`.
inline constexpr std::uint8_t kOpcodeSummon = 19;

inline constexpr std::uint8_t kOpcodeSetTopic = 39;
inline constexpr std::uint8_t kOpcodeMoveNpc = 40;

// Launch a sprite: `[animation u16][u8][from i32 x3][to i32 x3]`. The u16 is
// the Nth animation group of the sprite frame table, in the order `DSFT.BIN`
// stores them, on 154 of 154 uses — and the names are the traps their maps
// play: `fire04` bolts down Castle Darkmoor's halls, `dark08` in the sewer,
// thrown pillows, coins and stalactites in the haunted spiral, `null` and
// `Pending` on the placeholder maps. That it flies from the first point
// toward the second is `inferred` — the pairs are axis-aligned runs when the
// second is set, and it is zero on 83 uses. The u8 between is `unknown`
// (a speed?). Reproduce with `evt_info --launches`.
inline constexpr std::uint8_t kOpcodeLaunch = 21;

// The variable types whose meaning is established. A quest bit's value is
// the bit's number in `Quests.txt` (1..376 of 512 used); an item's is an
// `ITEMS.TXT` id (never past 578 across 641 uses); gold's is an amount.
// Everything else is treated as a numbered variable.
inline constexpr std::uint8_t kVarQuestBit = 16;
inline constexpr std::uint8_t kVarItem = 17;
inline constexpr std::uint8_t kVarGold = 21;

// Where an event sends the party.
struct MapTravel {
    int x = 0, y = 0, z = 0;
    int facing = 0;           // 0..2047, the angle scale MM6 uses
    std::string destination;  // a map file name; empty means this same map
};

// Read one travel step: four little-endian i32s — X, Y, Z, facing — then ten
// bytes not yet decoded, then the NUL-terminated destination at byte 26.
[[nodiscard]] inline std::optional<MapTravel> parse_travel(const ScriptStep& step) {
    if (step.opcode != kOpcodeTravel || step.arguments.size() < 27) {
        return std::nullopt;
    }
    const auto& a = step.arguments;
    const auto read = [&a](std::size_t at) {
        std::int32_t value = 0;
        for (std::size_t i = 4; i > 0; --i) {
            value = (value << 8) | a[at + i - 1];
        }
        return value;
    };
    MapTravel out;
    out.x = read(0);
    out.y = read(4);
    out.z = read(8);
    out.facing = read(12);
    for (std::size_t i = 26; i < a.size() && a[i] != 0; ++i) {
        out.destination += static_cast<char>(a[i]);
    }
    if (out.destination == "0") {
        out.destination.clear();
    }
    return out;
}

// A launched sprite: which animation, and the flight's two ends.
struct MapLaunch {
    int animation = 0;  // the Nth group of the sprite frame table
    int from_x = 0, from_y = 0, from_z = 0;
    int to_x = 0, to_y = 0, to_z = 0;

    // A launch with no second point; what it flies at is not stated.
    [[nodiscard]] bool aimless() const noexcept { return to_x == 0 && to_y == 0 && to_z == 0; }
};

[[nodiscard]] inline std::optional<MapLaunch> parse_launch(const ScriptStep& step) {
    if (step.opcode != kOpcodeLaunch || step.arguments.size() < 27) {
        return std::nullopt;
    }
    const auto& a = step.arguments;
    const auto read = [&a](std::size_t at) {
        std::int32_t value = 0;
        for (std::size_t i = 4; i > 0; --i) {
            value = (value << 8) | a[at + i - 1];
        }
        return value;
    };
    MapLaunch out;
    out.animation = a[0] | (a[1] << 8);
    out.from_x = read(3);
    out.from_y = read(7);
    out.from_z = read(11);
    out.to_x = read(15);
    out.to_y = read(19);
    out.to_z = read(23);
    return out;
}

// Whether this opcode's first argument is a string index.
[[nodiscard]] inline bool names_a_string(std::uint8_t opcode) noexcept {
    return opcode == kOpcodeMessage || opcode == kOpcodeLongMessage || opcode == kOpcodeName ||
           opcode == kOpcodeTitle;
}

enum class MapScriptError : std::uint8_t {
    None,
    // The container is too short, or its zlib stream will not inflate.
    BadContainer,
    // A record's declared size runs past the end of the payload, or is zero.
    BadRecord,
};

// A map's events, in file order.
class MapScript {
public:
    MapScript() = default;

    // `entry` is the raw stored bytes of the archive's `.EVT` entry.
    [[nodiscard]] static MapScriptError parse(std::span<const std::byte> entry, MapScript& out);

    [[nodiscard]] const std::vector<ScriptStep>& steps() const noexcept { return steps_; }
    [[nodiscard]] std::size_t size() const noexcept { return steps_.size(); }

    // The steps of one event, which are contiguous. Returns an empty span when
    // the map has no such event.
    [[nodiscard]] std::span<const ScriptStep> event(std::uint16_t id) const noexcept;

    // Whether this map defines an event at all — the question a face with an
    // event id asks.
    [[nodiscard]] bool defines(std::uint16_t id) const noexcept { return !event(id).empty(); }

    // The establishment an event enters, as a `2DEvents.txt` row id, or 0.
    [[nodiscard]] std::uint32_t building_of(std::uint16_t id) const noexcept {
        for (const auto& step : event(id)) {
            if (step.opcode == kOpcodeEnter && step.arguments.size() >= 4) {
                std::uint32_t value = 0;
                for (int i = 3; i >= 0; --i) {
                    value = (value << 8) | step.arguments[static_cast<std::size_t>(i)];
                }
                return value;
            }
        }
        return 0;
    }

    // Where an event sends the party, if anywhere. Note this reads the first
    // travel step unconditionally; the walker in game/script_walk.hpp is what
    // respects the checks in front of it.
    [[nodiscard]] std::optional<MapTravel> travel_of(std::uint16_t id) const {
        for (const auto& step : event(id)) {
            if (auto travel = parse_travel(step)) {
                return travel;
            }
        }
        return std::nullopt;
    }

    // The chest an event opens, or -1. Zero is a chest, so the absence of one
    // cannot be reported as zero.
    [[nodiscard]] int chest_of(std::uint16_t id) const noexcept {
        for (const auto& step : event(id)) {
            if (step.opcode == kOpcodeChest && !step.arguments.empty()) {
                return step.arguments.front();
            }
        }
        return -1;
    }

    // What the thing calls itself: the header's string index, or -1. Not
    // meaningful on an event that enters an establishment, whose header is
    // the `2DEvents.txt` row instead — ask `building_of` first.
    [[nodiscard]] int label_of(std::uint16_t id) const noexcept {
        for (const auto& step : event(id)) {
            if (step.opcode == kOpcodeHeader && !step.arguments.empty()) {
                return step.arguments.front();
            }
        }
        return -1;
    }

    // The string index an event's first step of this kind names, or -1. What
    // a door says when it is locked, or what a sign is called.
    [[nodiscard]] int string_of(std::uint16_t id, std::uint8_t opcode) const noexcept {
        for (const auto& step : event(id)) {
            if (step.opcode == opcode && !step.arguments.empty()) {
                return step.arguments.front();
            }
        }
        return -1;
    }

private:
    std::vector<ScriptStep> steps_;
};

// A map's `.STR`: NUL-terminated strings, which the script's records index.
class MapStrings {
public:
    MapStrings() = default;

    [[nodiscard]] static MapScriptError parse(std::span<const std::byte> entry, MapStrings& out);

    [[nodiscard]] const std::vector<std::string>& entries() const noexcept { return strings_; }
    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

    // Index into the table. Out of range answers with nothing rather than
    // failing: a script may name a string this install does not have.
    [[nodiscard]] std::string_view at(std::size_t index) const noexcept;

private:
    std::vector<std::string> strings_;
};

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_SCRIPT_HPP
