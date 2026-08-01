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

// Jump to one of up to six steps at random, zero-padded: every one of the
// 452 nonzero entries across 88 full uses is a step of its own event. The
// mine at D05 rolls between three payouts and two cave-ins — a step listed
// twice is favoured, which uniform choice over the slots reproduces — and
// D16's teleporter pads roll between four destinations. A zero entry falls
// through. Reproduce with `evt_info --asks`.
inline constexpr std::uint8_t kOpcodeRandomJump = 25;

// Ask for a typed answer: `[prompt u32][answer u32][answer u32][step u8]`.
// All three u32s are the map's own string indices and the byte is a step of
// its own event on 19 of 19 full uses — "What's the password?" answered by
// "JBARD" or "jbard", the Pyramid's "Answer?" by "kcopS", a Yes/No by "Yes"
// or "Y". A match jumps to the step; a miss falls through to what follows,
// which is where every one of these events keeps its "Wrong!". Matching
// ignoring case is `inferred` from the pairs being case variants.
inline constexpr std::uint8_t kOpcodeAsk = 26;

// Turn an event on or off: `[event u32][on/off u8]`. The byte is 0 or 1 on
// all 312 uses, and the id is an event of its own script on 175, of
// `GLOBAL.EVT` on another 106 — the fallback the engine's event lookup
// already takes — zero on 7, and on 12 the Oracle's uses name events of the
// Control Center next door, its sibling map. The remaining 12 resolve
// nowhere, like the 22 dangling face ids. That 0 disables and 1 enables is
// `inferred` from the Oracle's matched on/off sets. Reproduce with
// `evt_info --asks`.
inline constexpr std::uint8_t kOpcodeSwitch = 32;

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

// Hurt the party: `[target u8][element u8][amount u32]`. The element is an
// index in the resistance columns' own order — 0 physical, 1 fire,
// 2 electricity, 3 cold, 4 poison, 5 magic: the Pyramid's trap rooms sweep
// all six at amount 5, poison sits in the sewer and at Sweet Water's wells,
// electricity in the Control Center, and the haunted spiral lands a
// physical 1,000. Targets 0..3 are the four characters — one event
// addresses each in turn — and 4, 5 and 6 read as the user, the whole
// party and a random member: `inferred`, the majority uses ride the
// "Cave-in!" and "Ouch!" strings that hit everyone. Reproduce with
// `evt_info --catalog 9`.
inline constexpr std::uint8_t kOpcodeHarm = 9;

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

// More told their names by the prose join: a reward's give and the number
// its own event speaks sit side by side, and where the spoken number equals
// the given value the word after it names the type. Experience ("2000
// experience" beside a give of 2000, seven such, values 400..500,000 in big
// round numbers), food (1..10 a give), a cure's hit points and spell
// points, the seven attributes 32..38 given permanently — "+2 Luck
// permanent" is the barrel's own string — and the five resistances 46..50
// in the monster table's element order. `observed` for the joins;
// extending each name to that type's unspoken uses is `inferred`.
// Reproduce with `evt_info --currencies`.
// And the award: type 12's value is a filled `Awards.txt` row on 193 of 193
// checks, gives and sets — and the one a known quest sets, Goblinwatch's 53,
// reads "Solved the Goblinwatch Combination", the very quest whose reward
// event sets it. The sheet's honors are quest-given bits in an id space of
// their own. `observed`
inline constexpr std::uint8_t kVarAward = 12;
// And the chronicle: type 205's values are `Autonotes.txt` rows — the
// Seer's stage ladder writes 116 ("show the sixth letter to Andover
// Potbello") beside bit 81's line and 115 (the Ironfist letter) beside
// bit 82's, the notes chronicling exactly the stages their events speak.
// `observed` for those; extending the name to every 205 is `inferred`.
inline constexpr std::uint8_t kVarAutonote = 205;
// And found gold: type 22 pays where things are dug up rather than handed
// over — D05's "Gold vein" digs give 400..800 of it, the sewer's
// "Something's stashed here!" 1,000..2,000, D13's bone-piles a rising
// 1,000..3,500 — always round sums, never beside a spoken noun. That it is
// gold in the purse is `inferred` from the finds' own labels; it is the
// "gold you find" the Factor's and Banker's rows take their percent of.
inline constexpr std::uint8_t kVarGoldFound = 22;
inline constexpr std::uint8_t kVarHitPoints = 3;
inline constexpr std::uint8_t kVarSpellPoints = 5;
inline constexpr std::uint8_t kVarExperience = 13;

// **Variable 9 is the character's level — corrected from 8.** Both routines
// share one 225-byte selector (`0x4411c0` and `0x441ff0` hold the same bytes)
// and both index it with `id - 1`. Reading the selector *through* to the
// case, id 9 lands on the body that writes `word [esi + 0x32]`, and the
// max-hit-point getter at `0x481ebf` adds exactly `+0x32` and `+0x34`
// together — so `+0x32` is the level and id 9 sets it. The earlier reading
// took the jump-table entry number for the id and came out one low.
// `observed` at 0x440b4b and 0x481ebf.
//
// The four words run `+0x30`, `+0x32`, `+0x34`, `+0x36` for ids 8, 9, 10 and
// 11: an unnamed word, the level, the level's modifier, and the birth word.
inline constexpr std::uint8_t kVarLevel = 9;
inline constexpr std::uint8_t kVarLevelModifier = 10;
inline constexpr std::uint8_t kVarBirthWord = 11;
inline constexpr std::uint8_t kVarFood = 23;

// The rest of the setter's fifty-three bodies, each fixed by the offset its
// own first two instructions compute. `observed`
inline constexpr std::uint8_t kVarClass = 2;             // byte at +0x12
inline constexpr std::uint8_t kVarSex = 1;               // byte at +0x11
inline constexpr std::uint8_t kVarArmorClass = 8;        // the word at +0x30
inline constexpr std::uint8_t kVarHitPointsFull = 4;     // set to the maximum
inline constexpr std::uint8_t kVarSpellPointsFull = 6;   // set to the maximum
inline constexpr std::uint8_t kVarClearConditions = 104;  // wipes all eighteen

// **Six clocks that belong to no one but the scripts.** Ids 216..221 write
// eight-byte world-clock stamps into a fixed array at `0x90e19c + 8 × id`,
// just past the end of the party record. Three routines touch them — the
// setter at `0x441095`, the adder at `0x441ec7` and a clear at `0x4429ca` —
// and the getter at `0x44036c` reads one back, multiplies it by the **same
// 30/128 calendar float at `0x4b9374`** the world clock uses and divides by
// sixty, so a script gets its answer in game time.
//
// Nothing else in the executable reads them: those four sites are the only
// access to the array in the whole image. So they are neither interface state
// nor engine state — they are six timers the game keeps purely so that map
// scripts can stamp a moment and ask how long ago it was. `observed`
// **Property id 213 is a bit in the character's own array at `+0x1530`.**
// `0x4417f8` indexes it the way every bit array in this executable is
// indexed — `byte[base + (bit >> 3)]`, mask `0x80 >> (bit & 7)`. The array
// begins just after the readied spell at `+0x152f` and runs to where the
// stored terms start at `+0x1570`: **sixty-four bytes, five hundred and
// twelve bits**.
//
// That is far more room than the game's ninety-nine spells need, so the
// obvious name does not fit and is not taken. `observed` for the array, its
// span and its indexing; `unknown` for what the bits are.
inline constexpr std::uint8_t kVarCharacterBit = 213;
inline constexpr int kCharacterBitArray = 0x1530;
inline constexpr int kCharacterBitArrayBytes = 0x40;

// **Id 214** zeroes the two party bytes at `+0x95` and `+0x96` and then sets
// bit `0x80` on a record in a global array of **60-byte** entries at
// `0x6aef28`, counted by `0x6ba534`. `observed`; that the two bytes are the
// party's hireling slots is `inferred`, from their being cleared exactly when
// a record in that array is flagged.
inline constexpr std::uint8_t kVarHirelingMark = 214;

// **Id 215** adds a byte-masked amount to the reputation at `0x908d48`, which
// is party `+0xd8`. `observed`
inline constexpr std::uint8_t kVarReputation = 215;

inline constexpr std::uint8_t kVarTimerFirst = 216;
inline constexpr int kScriptTimerCount = 6;
// **The seven attributes are variables 31..37, not 32..38.** The setter's
// cases write `+0x14`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, `+0x28` and
// `+0x2c` for those seven ids, and the next seven — **38..44** — write the
// odd words between them, which are the attributes' *modifiers*. Two
// formulas confirm the mapping from the other side: max hit points asks the
// stat getter for id 3 and reads the stored pair at `+0x20`/`+0x22`, max
// spell points asks for id 2 and reads `+0x1c`/`+0x1e`. `observed` This
// engine had the run starting one too high. `0x440de7`..`0x440e94`.
// **Corrected again, from the setter's own case bodies.** The property
// dispatcher at `0x441303` gives each id a case, and the cases spell the
// mapping out with fixed offsets that leave nothing to read into them:
// id 32 adds to `+0x14` and id 38 to `+0x2c`, which are Might's and Luck's
// **bases**; id 25 adds to `+0x16` and id 31 to `+0x2e`, which are the same
// two **modifiers**. So the bases begin at 32 and the modifiers at 25 — and
// ids **39..45 land on the modifier bodies too**, a second name for the same
// seven fields. `observed` at 0x4419bc, 0x441ace, 0x4418ce and 0x44199a.
inline constexpr std::uint8_t kVarStatModFirst = 25;      // Might .. Luck modifiers
inline constexpr std::uint8_t kVarStatFirst = 32;         // Might .. Luck
inline constexpr std::uint8_t kVarStatModAlias = 39;      // the same seven again
// The five resistances are **words at `+0x1254`, in base and modifier pairs**,
// sitting immediately below the character's buff array at `+0x1268` — the
// same shape the attributes keep. Ids 46..50 write the bases and 51..55 the
// modifiers, and the two runs are not laid out in id order: by offset they
// go 46, 48, 47, 49, 50. `observed` at 0x441af0..0x441c5e.
inline constexpr std::uint8_t kVarResistFirst = 46;      // Fire, Elec, Cold, Poison, Magic
inline constexpr std::uint8_t kVarResistModFirst = 51;   // their modifiers

// Three more runs the same dispatcher names, each confirmed by the offset its
// body computes rather than by a fit:
//
//   * **56..86** are the thirty-one skills — the body indexes `id + 0x28`,
//     which is `+0x60` at id 56 and `+0x7e` at id 86, and it masks with
//     `0x3f`, keeps `0xc0` and stops at sixty, exactly as the trainer does;
//   * **87..103** are the seventeen conditions — the body writes
//     `+0x10c8 + 8 × id`, which is `+0x1380` at id 87, and it stamps the
//     world clock read straight out of the party record;
//   * **225** is the skill pool at `+0x1410`.
//
// `observed` at 0x441d89, 0x441dc3 and 0x44130a.
inline constexpr std::uint8_t kVarSkillFirst = 56;
inline constexpr std::uint8_t kVarConditionFirst = 87;
inline constexpr std::uint8_t kVarSkillPool = 225;

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
