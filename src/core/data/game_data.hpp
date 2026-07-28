#ifndef STARHAVEN_CORE_DATA_GAME_DATA_HPP
#define STARHAVEN_CORE_DATA_GAME_DATA_HPP

#include <filesystem>
#include <string_view>

#include "core/data/building_stats.hpp"
#include "core/data/interface_strings.hpp"
#include "core/data/item_generation.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/journal.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/merchant_text.hpp"
#include "core/data/name_table.hpp"
#include "core/data/npc_stats.hpp"
#include "core/data/profession_text.hpp"
#include "core/data/spell_stats.hpp"
#include "core/data/use_items.hpp"
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

// Load and parse `ITEMS.TXT` in one step. Item-instance ids index this table
// directly, including id zero.
[[nodiscard]] GameDataError load_item_stats(const std::filesystem::path& data_dir,
                                            ItemStatsTable& out);

[[nodiscard]] GameDataError load_random_items(const std::filesystem::path& data_dir,
                                              RandomItemTable& out);
[[nodiscard]] GameDataError load_standard_bonuses(const std::filesystem::path& data_dir,
                                                  StandardBonusTable& out);
[[nodiscard]] GameDataError load_use_items(const std::filesystem::path& data_dir,
                                           UseItemTable& out);
[[nodiscard]] GameDataError load_special_bonuses(const std::filesystem::path& data_dir,
                                                 SpecialBonusTable& out);

// Load and parse `Spells.txt` in one step.
[[nodiscard]] GameDataError load_spell_stats(const std::filesystem::path& data_dir,
                                             SpellStatsTable& out);

// Load and parse `2DEvents.txt` in one step.
[[nodiscard]] GameDataError load_building_stats(const std::filesystem::path& data_dir,
                                                BuildingStatsTable& out);

// Load and parse `NPCdata.txt` and `npcprof.txt`.
[[nodiscard]] GameDataError load_npcs(const std::filesystem::path& data_dir, NpcTable& out);
[[nodiscard]] GameDataError load_npc_professions(const std::filesystem::path& data_dir,
                                                 NpcProfessionTable& out);

// Load `npcbtb.txt`, the personality reaction and phrasing matrix.
[[nodiscard]] GameDataError load_npc_personalities(const std::filesystem::path& data_dir,
                                                   NpcPersonalityTable& out);

// Load and merge `npctopic.txt` with `npctext.txt`, and load `NPCNews.txt`.
[[nodiscard]] GameDataError load_npc_dialogue(const std::filesystem::path& data_dir,
                                              NpcDialogueTable& out);
[[nodiscard]] GameDataError load_npc_news(const std::filesystem::path& data_dir, NpcNewsTable& out);

// Load `PROFTEXT.txt`, what a hired NPC says on each day of the week.
[[nodiscard]] GameDataError load_profession_text(const std::filesystem::path& data_dir,
                                                 ProfessionTextTable& out);

// Load `Merchant.txt`, what a shopkeeper says at the counter.
[[nodiscard]] GameDataError load_merchant_text(const std::filesystem::path& data_dir,
                                               MerchantTextTable& out);

// Load `npcnames.txt`, the game's own list of given names.
[[nodiscard]] GameDataError load_names(const std::filesystem::path& data_dir, NameTable& out);

// Load `Global.txt`, the interface's own words.
[[nodiscard]] GameDataError load_interface_strings(const std::filesystem::path& data_dir,
                                                   InterfaceStrings& out);

// The three bit-keyed journal tables. Each knows its own column layout.
[[nodiscard]] GameDataError load_quests(const std::filesystem::path& data_dir, JournalTable& out);
[[nodiscard]] GameDataError load_awards(const std::filesystem::path& data_dir, JournalTable& out);
[[nodiscard]] GameDataError load_autonotes(const std::filesystem::path& data_dir,
                                           JournalTable& out);

// Load one of the name-and-prose tables: `Class.txt`, `stats.txt`,
// `SkillDes.txt`. `name` is the archive entry.
[[nodiscard]] GameDataError load_descriptions(const std::filesystem::path& data_dir,
                                              std::string_view name, DescriptionTable& out);

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_GAME_DATA_HPP
