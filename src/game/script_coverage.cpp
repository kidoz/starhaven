#include "game/script_coverage.hpp"

#include <algorithm>
#include <cctype>
#include <ostream>

#include "core/world/map_script.hpp"

namespace starhaven::game {

ScriptOpcodeCoverage script_opcode_coverage(std::uint8_t opcode) {
    using enum ScriptDispatch;
    using namespace world;
    switch (opcode) {
    case kOpcodeEnd:
        return {"End", Handled, 0};
    case kOpcodeEnter:
        return {"Enter", Handled, 4};
    case kOpcodeHeader:
        return {"Header", Metadata, 0};
    case kOpcodeTitle:
        return {"Title", Handled, 1};
    case kOpcodeTravel:
        return {"Travel", Handled, 27};
    case kOpcodeChest:
        return {"Chest", Handled, 1};
    case kOpcodeHarm:
        return {"Harm", Handled, 6};
    case kOpcodeRetexture:
        return {"Retexture", Handled, 5};
    case kOpcodeSetDecoration:
        return {"SetDecoration", Handled, 6};
    case kOpcodeCheck:
        return {"Check", Handled, 6};
    case kOpcodeDoor:
        return {"Door", Handled, 2};
    case kOpcodeGive:
        return {"Give", Handled, 5};
    case kOpcodeTake:
        return {"Take", Handled, 5};
    case kOpcodeSet:
        return {"Set", Handled, 5};
    case kOpcodeSummon:
        return {"Summon", Handled, 15};
    case kOpcodeLaunch:
        return {"Launch", Handled, 27};
    case kOpcodeRandomJump:
        return {"RandomJump", Handled, 6};
    case kOpcodeAsk:
        return {"Ask", Handled, 13};
    case kOpcodeMessage:
        return {"Message", Handled, 1};
    case kOpcodeLongMessage:
        return {"LongMessage", Handled, 1};
    case kOpcodeSwitch:
        return {"Switch", Handled, 5};
    case kOpcodeShowMessage:
        return {"ShowMessage", Handled, 0};
    case kOpcodeName:
        return {"Name", Handled, 1};
    case kOpcodeGoto:
        return {"Goto", Handled, 1};
    case kOpcodeSetTopic:
        return {"SetTopic", Handled, 9};
    case kOpcodeGenerateItem:
        return {"GenerateItem", Handled, 6};
    case kOpcodeSetDecorationEvent:
        return {"SetDecorationEvent", Handled, 4};
    case kOpcodeMoveNpc:
        return {"MoveNpc", Handled, 8};
    default:
        return {};
    }
}

bool original_opcode_has_handler(std::uint8_t opcode) {
    return opcode >= 1 && opcode <= 43 && opcode != 20 && opcode != 27 && opcode != 28 &&
           opcode != 31 && opcode != 37 && opcode != 38;
}

namespace {

std::string lower(std::string_view value) {
    std::string out;
    for (const unsigned char c : value) {
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

void count_step(ScriptCoverageCounts& counts, const world::ScriptStep& step) {
    const auto info = script_opcode_coverage(step.opcode);
    ++counts.records;
    switch (info.dispatch) {
    case ScriptDispatch::Handled:
        ++counts.dispatched;
        counts.short_arguments += step.arguments.size() < info.minimum_arguments ? 1 : 0;
        break;
    case ScriptDispatch::Metadata:
        ++counts.metadata;
        break;
    case ScriptDispatch::Unsupported:
        ++counts.unsupported;
        counts.original_handler_gaps += original_opcode_has_handler(step.opcode) ? 1 : 0;
        break;
    }
}

std::string escaped(std::string_view value) {
    std::string out;
    for (const char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

void write_counts(const ScriptCoverageCounts& counts, std::ostream& out) {
    out << counts.records << '\t' << counts.dispatched << '\t' << counts.metadata << '\t'
        << counts.unsupported << '\t' << counts.short_arguments << '\t'
        << counts.original_handler_gaps;
}

std::string_view dispatch_name(ScriptDispatch dispatch) {
    switch (dispatch) {
    case ScriptDispatch::Handled:
        return "dispatched";
    case ScriptDispatch::Metadata:
        return "metadata";
    case ScriptDispatch::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

}  // namespace

bool ScriptCoverage::complete() const {
    return !scripts.empty() &&
           std::ranges::all_of(scripts, [](const auto& script) { return script.error.empty(); });
}

bool ScriptCoverage::has_gaps() const {
    return counts.unsupported != 0 || counts.short_arguments != 0;
}

ScriptCoverage audit_script_coverage(const lod::LodArchive& icons) {
    ScriptCoverage out;
    std::vector<std::string> names;
    for (const auto& entry : icons.entries()) {
        if (lower(entry.name).ends_with(".evt")) {
            names.push_back(entry.name);
        }
    }
    std::ranges::sort(names);
    std::map<std::string, std::size_t> occurrences;
    for (const auto& name : names) {
        ++occurrences[lower(name)];
    }
    for (const auto& name : names) {
        ScriptCoverageFile file;
        file.name = name;
        std::span<const std::byte> raw;
        world::MapScript script;
        if (occurrences[lower(name)] != 1) {
            file.error = "duplicate_name";
        } else if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None) {
            file.error = "payload_error";
        } else {
            switch (world::MapScript::parse(raw, script)) {
            case world::MapScriptError::None:
                break;
            case world::MapScriptError::BadContainer:
                file.error = "bad_container";
                break;
            case world::MapScriptError::BadRecord:
                file.error = "bad_record";
                break;
            }
        }
        if (!file.error.empty()) {
            out.scripts.push_back(std::move(file));
            continue;
        }
        std::map<std::uint16_t, std::set<std::string>> features;
        for (const auto& step : script.steps()) {
            auto& event = features[step.event_id];
            using namespace world;
            if ((step.opcode == kOpcodeCheck || step.opcode == kOpcodeGive ||
                 step.opcode == kOpcodeTake || step.opcode == kOpcodeSet) &&
                step.arguments.size() >= script_opcode_coverage(step.opcode).minimum_arguments &&
                step.arguments[0] == kVarQuestBit) {
                event.insert("quest_bit");
            }
            switch (step.opcode) {
            case kOpcodeDoor:
                event.insert("door");
                break;
            case kOpcodeTravel:
                event.insert("travel");
                break;
            case kOpcodeEnter:
                event.insert("establishment");
                break;
            case kOpcodeChest:
                event.insert("chest");
                break;
            case kOpcodeSetTopic:
            case kOpcodeMoveNpc:
                event.insert("npc_mutation");
                break;
            default:
                break;
            }
        }
        file.events = features.size();
        out.events += file.events;
        std::size_t record = 0;
        for (const auto& step : script.steps()) {
            count_step(out.counts, step);
            count_step(file.counts, step);
            auto& op = out.opcodes[step.opcode];
            count_step(op.counts, step);
            op.scripts.insert(name);
            op.events.emplace(name, step.event_id);
            ++op.argument_sizes[step.arguments.size()];

            const auto info = script_opcode_coverage(step.opcode);
            const bool short_args = info.dispatch == ScriptDispatch::Handled &&
                                    step.arguments.size() < info.minimum_arguments;
            if (info.dispatch == ScriptDispatch::Unsupported || short_args) {
                std::string tags;
                for (const auto& feature : features[step.event_id]) {
                    if (!tags.empty()) {
                        tags += ',';
                    }
                    tags += feature;
                }
                out.locations.push_back({
                    name,
                    step.event_id,
                    step.sequence,
                    record,
                    step.opcode,
                    step.arguments.size(),
                    short_args,
                    std::move(tags),
                });
            }
            ++record;
        }
        out.scripts.push_back(std::move(file));
    }
    return out;
}

void write_script_coverage(const ScriptCoverage& coverage, std::ostream& out) {
    out << "# event-script-coverage-v1\n"
        << "# counts: records dispatched metadata unsupported short_arguments "
           "original_handler_gaps\n"
        << "# SUMMARY complete scripts_seen scripts_parsed events [counts]\n";
    const auto parsed = std::count_if(coverage.scripts.begin(), coverage.scripts.end(),
                                      [](const auto& file) { return file.error.empty(); });
    out << "SUMMARY\t" << (coverage.complete() ? 1 : 0) << '\t' << coverage.scripts.size() << '\t'
        << parsed << '\t' << coverage.events << '\t';
    write_counts(coverage.counts, out);
    out << "\n# OPCODE opcode name dispatch original_path scripts events [counts] "
           "argument_size:count,...\n";
    for (const auto& [opcode, stats] : coverage.opcodes) {
        const auto info = script_opcode_coverage(opcode);
        out << "OPCODE\t" << static_cast<int>(opcode) << '\t' << info.name << '\t'
            << dispatch_name(info.dispatch) << '\t'
            << (original_opcode_has_handler(opcode) ? "handler" : "default") << '\t'
            << stats.scripts.size() << '\t' << stats.events.size() << '\t';
        write_counts(stats.counts, out);
        out << '\t';
        bool first = true;
        for (const auto& [size, count] : stats.argument_sizes) {
            out << (first ? "" : ",") << size << ':' << count;
            first = false;
        }
        out << '\n';
    }
    out << "# SCRIPT filename status events [counts]\n";
    for (const auto& script : coverage.scripts) {
        out << "SCRIPT\t" << escaped(script.name) << '\t'
            << (script.error.empty() ? "ok" : script.error) << '\t' << script.events << '\t';
        write_counts(script.counts, out);
        out << '\n';
    }
    out << "# LOCATION filename event sequence record_index opcode argument_bytes status "
           "event_features\n";
    for (const auto& location : coverage.locations) {
        out << "LOCATION\t" << escaped(location.script) << '\t' << location.event << '\t'
            << static_cast<int>(location.sequence) << '\t' << location.record << '\t'
            << static_cast<int>(location.opcode) << '\t' << location.arguments << '\t'
            << (location.short_arguments ? "short_arguments" : "unsupported") << '\t'
            << location.event_features << '\n';
    }
}

}  // namespace starhaven::game
