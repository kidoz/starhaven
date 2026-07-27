#include "core/data/game_data.hpp"

#include <span>
#include <string>

#include "core/lod/lod_archive.hpp"

namespace starhaven::data {

namespace {

constexpr const char* kTableArchive = "icons.lod";
constexpr const char* kMapStatsEntry = "MapStats.txt";
constexpr const char* kItemStatsEntry = "ITEMS.TXT";
constexpr const char* kRandomItemsEntry = "RNDITEMS.TXT";
constexpr const char* kStandardBonusesEntry = "STDITEMS.TXT";
constexpr const char* kSpecialBonusesEntry = "SPCITEMS.TXT";

}  // namespace

GameDataError load_text_table(const std::filesystem::path& data_dir, std::string_view name,
                              TextTable& out) {
    lod::LodArchive archive;
    if (lod::LodArchive::open(data_dir / kTableArchive, archive) != lod::LodError::None) {
        return GameDataError::NoArchive;
    }
    std::span<const std::byte> entry;
    if (archive.payload(name, entry) != lod::LodArchive::PayloadError::None) {
        return GameDataError::NotFound;
    }
    if (TextTable::parse(entry, out) != TextTableError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_map_stats(const std::filesystem::path& data_dir, MapStatsTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, kMapStatsEntry, table);
        e != GameDataError::None) {
        return e;
    }
    if (MapStatsTable::parse(table, out) != MapStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_item_stats(const std::filesystem::path& data_dir, ItemStatsTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, kItemStatsEntry, table);
        e != GameDataError::None) {
        return e;
    }
    if (ItemStatsTable::parse(table, out) != ItemStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_random_items(const std::filesystem::path& data_dir, RandomItemTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, kRandomItemsEntry, table);
        e != GameDataError::None) {
        return e;
    }
    if (RandomItemTable::parse(table, out) != RandomItemError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_standard_bonuses(const std::filesystem::path& data_dir,
                                    StandardBonusTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, kStandardBonusesEntry, table);
        e != GameDataError::None) {
        return e;
    }
    if (StandardBonusTable::parse(table, out) != StandardBonusError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_special_bonuses(const std::filesystem::path& data_dir, SpecialBonusTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, kSpecialBonusesEntry, table);
        e != GameDataError::None) {
        return e;
    }
    if (SpecialBonusTable::parse(table, out) != SpecialBonusError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_spell_stats(const std::filesystem::path& data_dir, SpellStatsTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "Spells.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (SpellStatsTable::parse(table, out) != SpellStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_building_stats(const std::filesystem::path& data_dir, BuildingStatsTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "2DEvents.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (BuildingStatsTable::parse(table, out) != BuildingStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_npcs(const std::filesystem::path& data_dir, NpcTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "NPCdata.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (NpcTable::parse(table, out) != NpcStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_npc_professions(const std::filesystem::path& data_dir, NpcProfessionTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "npcprof.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (NpcProfessionTable::parse(table, out) != NpcStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_npc_personalities(const std::filesystem::path& data_dir,
                                     NpcPersonalityTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "npcbtb.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (NpcPersonalityTable::parse(table, out) != NpcStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_npc_dialogue(const std::filesystem::path& data_dir, NpcDialogueTable& out) {
    TextTable topics;
    TextTable texts;
    if (const GameDataError e = load_text_table(data_dir, "npctopic.txt", topics);
        e != GameDataError::None) {
        return e;
    }
    if (const GameDataError e = load_text_table(data_dir, "npctext.txt", texts);
        e != GameDataError::None) {
        return e;
    }
    if (NpcDialogueTable::parse(topics, texts, out) != NpcStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_npc_news(const std::filesystem::path& data_dir, NpcNewsTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "NPCNews.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (NpcNewsTable::parse(table, out) != NpcStatsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_profession_text(const std::filesystem::path& data_dir,
                                   ProfessionTextTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "PROFTEXT.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (ProfessionTextTable::parse(table, out) != ProfessionTextError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_names(const std::filesystem::path& data_dir, NameTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "npcnames.txt", table);
        e != GameDataError::None) {
        return e;
    }
    if (NameTable::parse(table, out) != NameTableError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

GameDataError load_interface_strings(const std::filesystem::path& data_dir, InterfaceStrings& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, "GLOBAL.TXT", table);
        e != GameDataError::None) {
        return e;
    }
    if (InterfaceStrings::parse(table, out) != InterfaceStringsError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

namespace {

GameDataError load_journal(const std::filesystem::path& data_dir, const char* entry,
                           std::size_t text_column, std::size_t category_column,
                           std::size_t notes_column, std::size_t alternate_column,
                           JournalTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, entry, table); e != GameDataError::None) {
        return e;
    }
    if (JournalTable::parse(table, text_column, category_column, notes_column, alternate_column,
                            out) != JournalError::None) {
        return GameDataError::BadTable;
    }
    return GameDataError::None;
}

}  // namespace

GameDataError load_quests(const std::filesystem::path& data_dir, JournalTable& out) {
    // "Q Bit | Actual Quest Note Text | Notes | Old Quest Note Text"
    return load_journal(data_dir, "Quests.txt", 1, JournalTable::kNoColumn, 2, 3, out);
}

GameDataError load_awards(const std::filesystem::path& data_dir, JournalTable& out) {
    // "A Bit | Awards | Notes"
    return load_journal(data_dir, "Awards.txt", 1, JournalTable::kNoColumn, 2,
                        JournalTable::kNoColumn, out);
}

GameDataError load_autonotes(const std::filesystem::path& data_dir, JournalTable& out) {
    // "Note bit | Autonote Text | Category". Autonotes.txt is an earlier and
    // shorter copy of the same table; this loads the current one.
    return load_journal(data_dir, "Autonote.txt", 1, 2, JournalTable::kNoColumn,
                        JournalTable::kNoColumn, out);
}

GameDataError load_descriptions(const std::filesystem::path& data_dir, std::string_view name,
                                DescriptionTable& out) {
    TextTable table;
    if (const GameDataError e = load_text_table(data_dir, name, table); e != GameDataError::None) {
        return e;
    }
    DescriptionTable::parse(table, out);
    return GameDataError::None;
}

}  // namespace starhaven::data
