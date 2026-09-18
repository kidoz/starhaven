#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "game/script_coverage.hpp"
#include "game/script_walk.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

using Bytes = std::vector<std::byte>;

void put(Bytes& bytes, std::size_t offset, std::uint32_t value, std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xffU);
    }
}

void put_text(Bytes& bytes, std::size_t offset, std::string_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        bytes.at(offset + i) = static_cast<std::byte>(value[i]);
    }
}

Bytes script_bytes(const std::vector<world::ScriptStep>& steps) {
    Bytes bytes(48, std::byte{0});
    for (const auto& step : steps) {
        const auto at = bytes.size();
        bytes.resize(at + 5 + step.arguments.size());
        put(bytes, at, static_cast<std::uint32_t>(4 + step.arguments.size()), 1);
        put(bytes, at + 1, step.event_id, 2);
        put(bytes, at + 3, step.sequence, 1);
        put(bytes, at + 4, step.opcode, 1);
        for (std::size_t i = 0; i < step.arguments.size(); ++i) {
            bytes[at + 5 + i] = static_cast<std::byte>(step.arguments[i]);
        }
    }
    return bytes;
}

struct Entry {
    std::string name;
    Bytes payload;
    bool compressed = false;
};

// Synthetic MMVI archive with directory offsets relative to byte 288.
lod::LodArchive archive(const std::vector<Entry>& entries) {
    Bytes bytes(288 + 32 * entries.size(), std::byte{0});
    put_text(bytes, 0, "LOD");
    put_text(bytes, 4, "MMVI");
    put_text(bytes, 0x100, "icons");
    put(bytes, 0x110, 288);
    put(bytes, 0x11c, static_cast<std::uint32_t>(entries.size()), 2);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const auto at = 288 + 32 * i;
        REQUIRE(entry.name.size() < 16);
        put_text(bytes, at, entry.name);
        put(bytes, at + 16, static_cast<std::uint32_t>(bytes.size() - 288));
        put(bytes, at + 20, static_cast<std::uint32_t>(entry.payload.size()));
        put(bytes, at + 24, entry.compressed ? 100 : 0);
        bytes.insert(bytes.end(), entry.payload.begin(), entry.payload.end());
    }
    lod::LodArchive out;
    const auto error = lod::LodArchive::parse(std::move(bytes), out);
    REQUIRE(error == lod::LodError::None);
    return out;
}

}  // namespace

TEST_CASE("coverage classification agrees with actual dispatch for every byte opcode",
          "[coverage]") {
    for (int opcode = 0; opcode <= 255; ++opcode) {
        CAPTURE(opcode);
        const auto op = static_cast<std::uint8_t>(opcode);
        world::MapScript script;
        REQUIRE(world::MapScript::parse(script_bytes({{1, 0, op, {}}}), script) ==
                world::MapScriptError::None);
        WalkState state;
        const auto outcome = walk_event(script, 1, state);
        REQUIRE(outcome.ran);
        const auto info = script_opcode_coverage(op);
        REQUIRE(outcome.unsupported.empty() == (info.dispatch != ScriptDispatch::Unsupported));
        if (info.dispatch == ScriptDispatch::Metadata) {
            REQUIRE_FALSE(outcome.acted());
        }
    }
}

TEST_CASE("audit counts every record while deduplicating scripts and scoped events", "[coverage]") {
    const auto bytes = script_bytes({
        {8, 0, world::kOpcodeHeader, {1}},
        {8, 0, world::kOpcodeDoor, {3, 1}},
        {8, 1, 3, {1, 2, 3, 4}},
        {8, 2, 3, {1, 2, 3, 4}},
        {9, 0, world::kOpcodeTravel, {}},
        {9, 1, 90, {}},
        {8, 3, world::kOpcodeTake, {world::kVarQuestBit, 1, 0, 0, 0}},
    });
    const auto coverage = audit_script_coverage(
        archive({{"b.evt", bytes}, {"A.EvT", bytes}, {"ignored.evt.bak", Bytes{std::byte{0}}}}));
    REQUIRE(coverage.complete());
    REQUIRE(coverage.has_gaps());
    REQUIRE(coverage.scripts.size() == 2);
    REQUIRE(coverage.scripts[0].name == "A.EvT");
    REQUIRE(coverage.events == 4);
    REQUIRE(coverage.counts.records == 14);
    REQUIRE(coverage.counts.dispatched == 6);
    REQUIRE(coverage.counts.metadata == 2);
    REQUIRE(coverage.counts.unsupported == 6);
    REQUIRE(coverage.counts.original_handler_gaps == 4);
    REQUIRE(coverage.counts.short_arguments == 2);
    REQUIRE(coverage.opcodes.at(3).scripts.size() == 2);
    REQUIRE(coverage.opcodes.at(3).events.size() == 2);
    REQUIRE(coverage.opcodes.at(3).argument_sizes.at(4) == 4);
    REQUIRE(coverage.locations.size() == 8);
    REQUIRE(coverage.locations.front().record == 2);
    REQUIRE(coverage.locations.front().event_features == "door,quest_bit");
    REQUIRE(coverage.locations[2].short_arguments);
    REQUIRE(coverage.locations[2].event_features == "travel");

    std::ostringstream first;
    std::ostringstream second;
    write_script_coverage(coverage, first);
    write_script_coverage(audit_script_coverage(archive({{"A.EvT", bytes}, {"b.evt", bytes}})),
                          second);
    REQUIRE(first.str() == second.str());
    REQUIRE(first.str().find("SUMMARY\t1\t2\t2\t4\t14\t6\t2\t6\t2\t4\n") != std::string::npos);
    REQUIRE(first.str().find("LOCATION\tA.EvT\t8\t1\t2\t3\t4\tunsupported\tdoor,quest_bit\n") !=
            std::string::npos);
}

TEST_CASE("argument guards are separate from opcode dispatch coverage", "[coverage]") {
    const auto guarded = script_bytes({
        {1, 0, world::kOpcodeDoor, {3}},
        {1, 1, world::kOpcodeDoor, {3, 1}},
        {1, 2, world::kOpcodeEnd, {}},
        {1, 3, world::kOpcodeHeader, {}},
    });
    const auto coverage = audit_script_coverage(archive({{"guards.EVT", guarded}}));
    REQUIRE(coverage.counts.dispatched == 3);
    REQUIRE(coverage.counts.short_arguments == 1);
    REQUIRE(coverage.counts.unsupported == 0);
    REQUIRE(coverage.has_gaps());
    const auto valid =
        script_bytes({{1, 0, world::kOpcodeDoor, {3, 1}}, {1, 1, world::kOpcodeEnd, {}}});
    const auto clean = audit_script_coverage(archive({{"clean.EVT", valid}}));
    REQUIRE(clean.complete());
    REQUIRE_FALSE(clean.has_gaps());
}

TEST_CASE("failed and ambiguous script entries cannot produce a complete audit", "[coverage]") {
    const auto good = script_bytes({{1, 0, world::kOpcodeEnd, {}}});
    auto malformed = good;
    malformed.back() = std::byte{1};
    malformed.push_back(std::byte{2});  // truncated next record
    const auto coverage = audit_script_coverage(archive({
        {"good.evt", good},
        {"short.evt", Bytes{std::byte{0}}},
        {"bad.evt", malformed},
        {"packed.evt", good, true},
        {"dupe.evt", good},
        {"DUPE.EVT", good},
    }));
    REQUIRE_FALSE(coverage.complete());
    REQUIRE(coverage.scripts.size() == 6);
    REQUIRE(coverage.counts.records == 1);
    std::map<std::string, std::size_t> failures;
    for (const auto& file : coverage.scripts) {
        ++failures[file.error];
    }
    REQUIRE(failures.at("bad_container") == 1);
    REQUIRE(failures.at("bad_record") == 1);
    REQUIRE(failures.at("payload_error") == 1);
    REQUIRE(failures.at("duplicate_name") == 2);
    REQUIRE_FALSE(audit_script_coverage(archive({})).complete());
    REQUIRE_FALSE(audit_script_coverage(archive({{"text.str", good}})).complete());
}

TEST_CASE("TSV escapes archive filenames without exposing script arguments", "[coverage]") {
    const auto coverage = audit_script_coverage(
        archive({{"a\t\n\\.evt", script_bytes({{1, 0, 42, {'s', 'e', 'c', 'r', 'e', 't'}}})}}));
    std::ostringstream text;
    write_script_coverage(coverage, text);
    REQUIRE(text.str().find("a\\t\\n\\\\.evt") != std::string::npos);
    REQUIRE(text.str().find("secret") == std::string::npos);
}

TEST_CASE("the original dispatch default is distinguished from missing real handlers",
          "[coverage]") {
    for (const std::uint8_t opcode : {0, 20, 27, 28, 31, 37, 38, 44, 90, 255}) {
        REQUIRE_FALSE(original_opcode_has_handler(opcode));
    }
    for (const std::uint8_t opcode : {1, 3, 13, 33, 42, 43}) {
        REQUIRE(original_opcode_has_handler(opcode));
    }
}
