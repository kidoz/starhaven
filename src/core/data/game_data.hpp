#ifndef STARHAVEN_CORE_DATA_GAME_DATA_HPP
#define STARHAVEN_CORE_DATA_GAME_DATA_HPP

#include <filesystem>
#include <string_view>

#include "core/data/map_stats.hpp"
#include "core/data/text_table.hpp"

namespace starhaven::data {

enum class GameDataError {
    None,
    // `icons.lod` is missing or unreadable under the given data directory.
    NoArchive,
    // The archive holds no entry of that name.
    NotFound,
    // The entry is there but did not parse as a table.
    BadTable,
};

// Load one of the shipped data tables out of an installation's `icons.lod`.
// `name` is the archive entry name, e.g. "MapStats.txt" (matched
// case-insensitively, as archive names are mixed-case).
[[nodiscard]] GameDataError load_text_table(const std::filesystem::path& data_dir,
                                            std::string_view name, TextTable& out);

// Load and parse `MapStats.txt` in one step. This is what a caller wanting a
// map's display name or music track needs, and it keeps the entry name in one
// place rather than spelled out at each call site.
[[nodiscard]] GameDataError load_map_stats(const std::filesystem::path& data_dir,
                                           MapStatsTable& out);

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_GAME_DATA_HPP
