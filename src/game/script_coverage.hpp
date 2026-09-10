#ifndef STARHAVEN_GAME_SCRIPT_COVERAGE_HPP
#define STARHAVEN_GAME_SCRIPT_COVERAGE_HPP

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/lod/lod_archive.hpp"

namespace starhaven::game {

enum class ScriptDispatch : std::uint8_t { Handled, Metadata, Unsupported };

struct ScriptOpcodeCoverage {
    std::string_view name = "unknown";
    ScriptDispatch dispatch = ScriptDispatch::Unsupported;
    std::size_t minimum_arguments = 0;
};

// Dispatch presence, not semantic compatibility. Kept honest by an exhaustive
// synthetic probe of walk_event in test_script_coverage.cpp.
[[nodiscard]] ScriptOpcodeCoverage script_opcode_coverage(std::uint8_t opcode);

// Pinned to the original MM6 dispatch table documented in map-events.md.
// This is evidence for audit triage, not a change to runtime dispatch policy.
[[nodiscard]] bool original_opcode_has_handler(std::uint8_t opcode);

struct ScriptCoverageCounts {
    std::size_t records = 0;
    std::size_t dispatched = 0;
    std::size_t metadata = 0;
    std::size_t unsupported = 0;
    std::size_t short_arguments = 0;        // subset of dispatched, below current guard
    std::size_t original_handler_gaps = 0;  // subset of unsupported
};

struct ScriptCoverageOpcode {
    ScriptCoverageCounts counts;
    std::set<std::string> scripts;
    std::set<std::pair<std::string, std::uint16_t>> events;
    std::map<std::size_t, std::size_t> argument_sizes;
};

struct ScriptCoverageFile {
    std::string name;
    std::string error;
    std::size_t events = 0;
    ScriptCoverageCounts counts;
};

struct ScriptCoverageLocation {
    std::string script;
    std::uint16_t event = 0;
    std::uint8_t sequence = 0;
    std::size_t record = 0;  // zero-based index in the parsed script
    std::uint8_t opcode = 0;
    std::size_t arguments = 0;
    bool short_arguments = false;
    std::string event_features;  // co-occurrence, not a reachability claim
};

struct ScriptCoverage {
    ScriptCoverageCounts counts;
    std::size_t events = 0;  // unique (script name, event id) pairs
    std::vector<ScriptCoverageFile> scripts;
    std::map<std::uint8_t, ScriptCoverageOpcode> opcodes;
    std::vector<ScriptCoverageLocation> locations;

    [[nodiscard]] bool complete() const;
    [[nodiscard]] bool has_gaps() const;
};

// Reads every case-insensitive .EVT entry. Failures remain visible in the
// result; an archive with no scripts is not a successful audit.
[[nodiscard]] ScriptCoverage audit_script_coverage(const lod::LodArchive& icons);

// Deterministic TSV, with row schemas in comments. Never emits arguments or
// localized text. Filenames are escaped to keep each record on one line.
void write_script_coverage(const ScriptCoverage& coverage, std::ostream& out);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_COVERAGE_HPP
