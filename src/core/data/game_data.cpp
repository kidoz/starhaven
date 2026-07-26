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
