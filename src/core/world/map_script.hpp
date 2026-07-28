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
// What it declares is still unread.
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
