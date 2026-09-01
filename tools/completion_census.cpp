// The completion census: where every quest bit, award, and autonote is
// set, cleared, or checked across every shipped event script.
//
// The completion contract (see .agents/contexts/plans) needs to know which
// journal bits the shipped scripts can actually reach. This tool walks all
// the `.EVT` scripts in `icons.lod` — the maps' own and `GLOBAL.EVT` — and
// counts, per `Quests.txt`, `Awards.txt` and `Autonotes.txt` row, the events
// that set it, clear it, or check it. A row no script sets cannot be
// completed by playing; a row with no text cannot be shown.
//
// Output is one line per row, greppable, with up to six sources each.
// `--text` adds the row's own text for local research; the output is a
// census, not a redistribution.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>

#include "core/data/game_data.hpp"
#include "core/data/journal.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/world/map_script.hpp"

namespace {

constexpr int kTypeQuestBit = 16;   // world::kVarQuestBit
constexpr int kTypeAward = 12;      // world::kVarAward
constexpr int kTypeAutonote = 205;  // world::kVarAutonote

// Two of the 83 scripts ship with a lowercase extension — D08.evt and
// Pyramid.evt — so the extension test must not care about case.
bool is_script(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    const std::string tail = name.substr(name.size() - 4);
    return tail[0] == '.' && std::tolower(static_cast<unsigned char>(tail[1])) == 'e' &&
           std::tolower(static_cast<unsigned char>(tail[2])) == 'v' &&
           std::tolower(static_cast<unsigned char>(tail[3])) == 't';
}

struct BitCensus {
    int set = 0;
    int clear = 0;
    int check = 0;
    std::string sources;  // up to six "EVT:id", comma-separated
};

void note(BitCensus& census, int kind, const std::string& script, int event_id) {
    if (kind == 0) {
        ++census.set;
    } else if (kind == 1) {
        ++census.clear;
    } else {
        ++census.check;
    }
    // Six sources is enough to find any of them again in evt_info.
    if (std::count(census.sources.begin(), census.sources.end(), ',') < 5) {
        if (!census.sources.empty()) {
            census.sources += ',';
        }
        census.sources += script + ":" + std::to_string(event_id);
    }
}

int run(const std::filesystem::path& data_dir, bool with_text) {
    starhaven::lod::LodArchive icons;
    if (starhaven::lod::LodArchive::open(data_dir / "icons.lod", icons) !=
        starhaven::lod::LodError::None) {
        std::cerr << "error: could not open icons.lod under " << data_dir << "\n";
        return 2;
    }

    starhaven::data::JournalTable quests;
    starhaven::data::JournalTable awards;
    starhaven::data::JournalTable autonotes;
    (void)starhaven::data::load_quests(data_dir, quests);
    (void)starhaven::data::load_awards(data_dir, awards);
    (void)starhaven::data::load_autonotes(data_dir, autonotes);

    std::map<int, BitCensus> quest_bits;
    std::map<int, BitCensus> award_bits;
    std::map<int, BitCensus> note_bits;
    int scripts = 0;

    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        std::span<const std::byte> raw;
        if (icons.payload(entry.name, raw) != starhaven::lod::LodArchive::PayloadError::None) {
            continue;
        }
        starhaven::world::MapScript script;
        if (starhaven::world::MapScript::parse(raw, script) !=
            starhaven::world::MapScriptError::None) {
            continue;
        }
        ++scripts;
        for (const auto& step : script.steps()) {
            if (step.arguments.empty()) {
                continue;
            }
            int kind = -1;
            if (step.opcode == starhaven::world::kOpcodeGive ||
                step.opcode == starhaven::world::kOpcodeSet) {
                kind = 0;
            } else if (step.opcode == starhaven::world::kOpcodeTake) {
                kind = 1;
            } else if (step.opcode == starhaven::world::kOpcodeCheck) {
                kind = 2;
            }
            if (kind < 0) {
                continue;
            }
            const int type = step.arguments.front();
            if (type != kTypeQuestBit && type != kTypeAward && type != kTypeAutonote) {
                continue;
            }
            // The value is the little-endian u32 after the type, the same
            // read the walker's `value_of` makes.
            std::uint32_t raw_value = 0;
            if (step.arguments.size() >= 5) {
                for (std::size_t i = 1; i < 5; ++i) {
                    raw_value |= static_cast<std::uint32_t>(step.arguments[i]) << ((i - 1) * 8);
                }
            }
            const int value = static_cast<int>(raw_value);
            BitCensus* census = &quest_bits[value];
            if (type == kTypeAward) {
                census = &award_bits[value];
            } else if (type == kTypeAutonote) {
                census = &note_bits[value];
            }
            note(*census, kind, entry.name, step.event_id);
        }
    }

    const auto dump = [&with_text](const char* what, const starhaven::data::JournalTable& table,
                                   std::map<int, BitCensus>& census) {
        std::size_t text_rows = 0;
        std::size_t settable = 0;
        for (const auto& row : table.entries()) {
            if (row.bit <= 0) {
                continue;
            }
            const BitCensus& seen = census[row.bit];
            const int has_text = row.text.empty() ? 0 : 1;
            if (!seen.sources.empty()) {
                ++settable;
            }
            if (!row.text.empty()) {
                ++text_rows;
            }
            std::cout << what << ' ' << row.bit << " text=" << has_text << " set=" << seen.set
                      << " clear=" << seen.clear << " check=" << seen.check;
            if (!seen.sources.empty()) {
                std::cout << " src=" << seen.sources;
            }
            if (with_text && !row.text.empty()) {
                std::cout << " | " << row.text;
            }
            std::cout << '\n';
        }
        std::cout << what << "-summary rows=" << table.entries().size() << " text=" << text_rows
                  << " settable=" << settable << '\n';
    };

    std::cout << "scripts=" << scripts << '\n';
    dump("QUEST", quests, quest_bits);
    dump("AWARD", awards, award_bits);
    dump("NOTE", autonotes, note_bits);
    return 0;
}

}  // namespace

int main(int argc, const char* const* argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <data-dir> [--text]\n";
        return 2;
    }
    try {
        const bool with_text = argc > 2 && std::string(argv[2]) == "--text";
        return run(std::filesystem::path(argv[1]), with_text);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 2;
    }
}
