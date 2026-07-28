#ifndef STARHAVEN_CORE_WORLD_MAP_SCRIPT_HPP
#define STARHAVEN_CORE_WORLD_MAP_SCRIPT_HPP

// A map's event script and its strings.
//
// Every map has a `.EVT` and a `.STR` entry in `icons.lod` — not in
// `Games.lod` with the geometry. The `.EVT` is a flat run of size-prefixed
// records grouped by event id; the `.STR` is the strings those records refer
// to. See docs/formats/map-events.md.

#include <cstdint>
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
