#ifndef STARHAVEN_GAME_SCRIPT_WALK_HPP
#define STARHAVEN_GAME_SCRIPT_WALK_HPP

// Walking an event, step by step, the way using a door or a switch runs it.
//
// The machinery is the decoded conditional set: a check jumps to a step of
// its own event when it passes, give/take/set move a typed variable, goto
// jumps unconditionally, and end stops. Quest bits, items and gold are the
// three types whose meaning is established; every other type is a numbered
// variable this walker holds. What a check passing means — at least, rather
// than exactly — is `inferred`; both shipped flows read naturally either
// way. See docs/formats/map-events.md.

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "core/world/map_script.hpp"

namespace starhaven::game {

// How many steps one use may execute before the walker calls it a loop.
inline constexpr int kWalkBudget = 256;

// What the party brings to a walk. Bits and variables persist between walks;
// items and gold are the caller's view of the packs and the purse, refreshed
// before each walk and read back after.
struct WalkState {
    std::set<int> bits;
    std::map<int, int> variables;
    std::vector<int> items;
    int gold = 0;

    // The quest chain's NPC rewrites, persistent like the bits: topic slots
    // overridden per (npc, slot) — zero clears — and where an NPC has been
    // moved, zero meaning away.
    std::map<std::pair<int, int>, int> npc_topics;
    std::map<int, int> npc_places;

    // What the random-jump opcode rolls with: a small xorshift state the
    // walker advances on each roll, so a test can pin the dice.
    std::uint32_t luck = 0x9E3779B9u;
};

// What one use of one event did.
struct WalkOutcome {
    bool ran = false;             // the map defines the event
    std::vector<int> said;        // message string indices, in walk order
    std::vector<int> given;       // item ids that entered the packs
    std::vector<int> taken;       // item ids that left them
    std::uint32_t building = 0;   // a counter to open, or 0
    int chest = -1;               // a chest to open, or -1
    std::optional<world::MapTravel> travel;

    // Faces to re-texture: a thrown switch is drawn thrown.
    std::vector<std::pair<std::uint32_t, std::string>> retextures;

    // Doors to move: the id opcode 15 throws, and its state byte — 0 shuts,
    // 1 opens, and the rare 2 reads as a toggle. `inferred`
    std::vector<std::pair<int, int>> doors;

    // Monsters to summon: the map's encounter slot, the A/B/C variant, how
    // many, and where, in MM6's own coordinates.
    struct Summon {
        int slot = 0, variant = 0, count = 0;
        int x = 0, y = 0, z = 0;
    };
    std::vector<Summon> summons;

    // Sprites to launch, in MM6's own coordinates. What flies is the
    // record's; how fast, and that an aimless one flies at the party, are
    // the session's.
    std::vector<world::MapLaunch> launches;

    // A question the event stopped at: string indices for the prompt and
    // the two spellings of the accepted answer, the step a match jumps to,
    // and the one a miss falls through to. The caller collects the typing
    // and walks the event again from whichever step the answer earns.
    struct Ask {
        int prompt = 0;
        int answer_a = 0;
        int answer_b = 0;
        std::uint8_t step_on_match = 0;
        std::uint8_t step_on_miss = 0;
    };
    std::optional<Ask> ask;

    // Whether anything observable happened, which is what decides if the
    // strike that ran the event was consumed by it.
    [[nodiscard]] bool acted() const noexcept {
        return !said.empty() || !given.empty() || !taken.empty() || building != 0 ||
               chest >= 0 || travel.has_value() || !retextures.empty() || !doors.empty() ||
               !summons.empty() || !launches.empty() || ask.has_value();
    }
};

// Run one event against the party's state. Opcodes that are not yet decoded
// are skipped, which errs toward a door that works over one that jams.
// `resume_at` walks from a named sequence instead of the top — how an
// answered question continues at the step its answer earned.
[[nodiscard]] inline WalkOutcome walk_event(const world::MapScript& script, std::uint16_t id,
                                            WalkState& state, int resume_at = -1) {
    WalkOutcome out;
    const auto steps = script.event(id);
    if (steps.empty()) {
        return out;
    }
    out.ran = true;

    const auto value_of = [](const world::ScriptStep& step) {
        std::uint32_t value = 0;
        for (std::size_t i = 4; i >= 1; --i) {
            value = (value << 8) | step.arguments[i];
        }
        return static_cast<int>(value);
    };
    // A jump names a sequence number; the header shares sequence zero with
    // the first real step, so it is excluded from the search.
    const auto step_at = [&steps](std::uint8_t sequence) {
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (steps[i].sequence == sequence && steps[i].opcode != world::kOpcodeHeader) {
                return i;
            }
        }
        return steps.size();
    };

    std::size_t at = 0;
    if (resume_at >= 0) {
        at = step_at(static_cast<std::uint8_t>(resume_at));
    }
    for (int budget = kWalkBudget; at < steps.size() && budget > 0; --budget) {
        const auto& step = steps[at];
        const auto& a = step.arguments;
        ++at;
        switch (step.opcode) {
        case world::kOpcodeEnd:
            return out;
        case world::kOpcodeCheck: {
            if (a.size() < 6) {
                break;
            }
            const int type = a[0];
            const int value = value_of(step);
            bool passes = false;
            switch (type) {
            case world::kVarQuestBit:
                passes = state.bits.contains(value);
                break;
            case world::kVarItem:
                passes = std::find(state.items.begin(), state.items.end(), value) !=
                         state.items.end();
                break;
            case world::kVarGold:
                passes = state.gold >= value;
                break;
            default:
                passes = state.variables[type] >= value;
                break;
            }
            if (passes) {
                at = step_at(a[5]);
            }
            break;
        }
        case world::kOpcodeGive:
        case world::kOpcodeTake:
        case world::kOpcodeSet: {
            if (a.size() < 5) {
                break;
            }
            const int type = a[0];
            const int value = value_of(step);
            const bool take = step.opcode == world::kOpcodeTake;
            switch (type) {
            case world::kVarQuestBit:
                if (take) {
                    state.bits.erase(value);
                } else {
                    state.bits.insert(value);
                }
                break;
            case world::kVarItem:
                if (take) {
                    if (const auto it = std::find(state.items.begin(), state.items.end(), value);
                        it != state.items.end()) {
                        state.items.erase(it);
                        out.taken.push_back(value);
                    }
                } else {
                    state.items.push_back(value);
                    out.given.push_back(value);
                }
                break;
            case world::kVarGold:
                state.gold += take ? -value : value;
                state.gold = state.gold < 0 ? 0 : state.gold;
                break;
            default:
                if (step.opcode == world::kOpcodeSet) {
                    state.variables[type] = value;
                } else {
                    state.variables[type] += take ? -value : value;
                }
                break;
            }
            break;
        }
        case world::kOpcodeMessage:
        case world::kOpcodeLongMessage:
            if (!a.empty()) {
                out.said.push_back(a.front());
            }
            break;
        case world::kOpcodeEnter:
            if (a.size() >= 4) {
                std::uint32_t value = 0;
                for (int i = 3; i >= 0; --i) {
                    value = (value << 8) | a[static_cast<std::size_t>(i)];
                }
                out.building = value;
            }
            return out;
        case world::kOpcodeChest:
            if (!a.empty()) {
                out.chest = a.front();
            }
            return out;
        case world::kOpcodeTravel:
            if (auto travel = world::parse_travel(step)) {
                out.travel = std::move(travel);
                return out;
            }
            break;
        case world::kOpcodeDoor:
            if (a.size() >= 2) {
                out.doors.emplace_back(a[0], a[1]);
            }
            break;
        case world::kOpcodeRetexture: {
            if (a.size() < 5) {
                break;
            }
            std::uint32_t face = 0;
            for (int i = 3; i >= 0; --i) {
                face = (face << 8) | a[static_cast<std::size_t>(i)];
            }
            std::string texture;
            for (std::size_t i = 4; i < a.size() && a[i] != 0; ++i) {
                texture += static_cast<char>(a[i]);
            }
            if (!texture.empty()) {
                out.retextures.emplace_back(face, std::move(texture));
            }
            break;
        }
        case world::kOpcodeSummon:
            if (a.size() >= 15) {
                WalkOutcome::Summon summon;
                summon.slot = a[0];
                summon.variant = a[1];
                summon.count = a[2];
                const auto i32_at = [&a](std::size_t at) {
                    std::uint32_t v = 0;
                    for (int i = 3; i >= 0; --i) {
                        v = (v << 8) | a[at + static_cast<std::size_t>(i)];
                    }
                    return static_cast<std::int32_t>(v);
                };
                summon.x = i32_at(3);
                summon.y = i32_at(7);
                summon.z = i32_at(11);
                out.summons.push_back(summon);
            }
            break;
        case world::kOpcodeLaunch:
            if (auto launch = world::parse_launch(step)) {
                out.launches.push_back(*launch);
            }
            break;
        case world::kOpcodeRandomJump: {
            if (a.size() < 6) {
                break;
            }
            // One of the six slots, rolled by the state's own dice; a zero
            // slot falls through to the next step.
            state.luck ^= state.luck << 13;
            state.luck ^= state.luck >> 17;
            state.luck ^= state.luck << 5;
            const std::uint8_t slot = a[state.luck % 6];
            if (slot != 0) {
                at = step_at(slot);
            }
            break;
        }
        case world::kOpcodeAsk: {
            if (a.size() < 13) {
                break;
            }
            // The walk stops at a question; the caller returns with the
            // typed answer's verdict via `resume_at`.
            WalkOutcome::Ask ask;
            ask.prompt = static_cast<int>(a[0]) | (a[1] << 8);
            ask.answer_a = static_cast<int>(a[4]) | (a[5] << 8);
            ask.answer_b = static_cast<int>(a[8]) | (a[9] << 8);
            ask.step_on_match = a[12];
            ask.step_on_miss = at < steps.size() ? steps[at].sequence : step.sequence;
            out.ask = ask;
            return out;
        }
        case world::kOpcodeSetTopic:
            if (a.size() >= 9 && a[4] < 3) {
                std::uint32_t npc = 0, topic = 0;
                for (int i = 3; i >= 0; --i) {
                    npc = (npc << 8) | a[static_cast<std::size_t>(i)];
                    topic = (topic << 8) | a[static_cast<std::size_t>(i + 5)];
                }
                state.npc_topics[{static_cast<int>(npc), a[4]}] = static_cast<int>(topic);
            }
            break;
        case world::kOpcodeMoveNpc:
            if (a.size() >= 8) {
                std::uint32_t npc = 0, place = 0;
                for (int i = 3; i >= 0; --i) {
                    npc = (npc << 8) | a[static_cast<std::size_t>(i)];
                    place = (place << 8) | a[static_cast<std::size_t>(i + 4)];
                }
                state.npc_places[static_cast<int>(npc)] = static_cast<int>(place);
            }
            break;
        case world::kOpcodeGoto:
            if (!a.empty()) {
                at = step_at(a.front());
            }
            break;
        default:
            break;  // headers, names, doors, and the undecoded rest
        }
    }
    return out;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_WALK_HPP
