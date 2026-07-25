#include "core/data/game_data.hpp"

#include <span>
#include <string>

#include "core/lod/lod_archive.hpp"

namespace starhaven::data {

namespace {

constexpr const char* kTableArchive = "icons.lod";
constexpr const char* kMapStatsEntry = "MapStats.txt";
constexpr const char* kItemStatsEntry = "ITEMS.TXT";

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

}  // namespace starhaven::data
