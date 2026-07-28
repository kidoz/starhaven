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

    // Whether anything observable happened, which is what decides if the
    // strike that ran the event was consumed by it.
    [[nodiscard]] bool acted() const noexcept {
        return !said.empty() || !given.empty() || !taken.empty() || building != 0 ||
               chest >= 0 || travel.has_value();
    }
};

// Run one event against the party's state. Opcodes that are not yet decoded
// are skipped, which errs toward a door that works over one that jams.
[[nodiscard]] inline WalkOutcome walk_event(const world::MapScript& script, std::uint16_t id,
                                            WalkState& state) {
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
