#include "core/data/npc_stats.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

// NPCdata.txt
constexpr std::size_t kNpcId = 0;
constexpr std::size_t kNpcName = 1;
constexpr std::size_t kNpcPicture = 2;
constexpr std::size_t kNpcState = 3;
constexpr std::size_t kNpcFame = 4;
constexpr std::size_t kNpcReputation = 5;
constexpr std::size_t kNpcBuilding = 6;
constexpr std::size_t kNpcProfession = 7;
constexpr std::size_t kNpcJoin = 8;
constexpr std::size_t kNpcNews = 9;
constexpr std::size_t kNpcEventA = 10;
constexpr std::size_t kNpcNotes = 13;

// npcprof.txt
constexpr std::size_t kProfId = 0;
constexpr std::size_t kProfName = 1;
constexpr std::size_t kProfChance = 2;
constexpr std::size_t kProfCost = 3;
constexpr std::size_t kProfPersonality = 4;
constexpr std::size_t kProfAction = 5;
constexpr std::size_t kProfBenefit = 6;
constexpr std::size_t kProfJoinText = 7;

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

// The tables mark a yes/no column with 1 and 0, not with letters, despite the
// heading reading "Y / N".
bool cell_flag(const TextTable& t, std::size_t row, std::size_t col) {
    return t.cell_int(row, col) != 0;
}

std::size_t find_header(const TextTable& table, std::size_t column, std::string_view label) {
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, column)), label)) {
            return r;
        }
    }
    return table.row_count();
}

}  // namespace

NpcStatsError NpcTable::parse(const TextTable& table, NpcTable& out) {
    out.entries_.clear();

    const std::size_t header = find_header(table, kNpcName, "Name");
    if (header == table.row_count()) {
        return NpcStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const int id = table.cell_int(r, kNpcId, -1);
        std::string name = cell_text(table, r, kNpcName);
        if (id <= 0 || name.empty()) {
            continue;
        }
        NpcEntry e;
        e.id = id;
        e.name = std::move(name);
        e.picture = table.cell_int(r, kNpcPicture);
        e.state = table.cell_int(r, kNpcState);
        e.fame = table.cell_int(r, kNpcFame);
        e.reputation = table.cell_int(r, kNpcReputation);
        e.building_id = table.cell_int(r, kNpcBuilding);
        e.profession_id = table.cell_int(r, kNpcProfession);
        e.can_join = cell_flag(table, r, kNpcJoin);
        e.has_news = cell_flag(table, r, kNpcNews);
        for (std::size_t i = 0; i < e.events.size(); ++i) {
            e.events[i] = table.cell_int(r, kNpcEventA + i);
        }
        e.notes = cell_text(table, r, kNpcNotes);
        out.entries_.push_back(std::move(e));
    }
    return NpcStatsError::None;
}

std::vector<const NpcEntry*> NpcTable::in_building(int building_id) const {
    std::vector<const NpcEntry*> out;
    if (building_id <= 0) {
        return out;
    }
    for (const auto& e : entries_) {
        if (e.building_id == building_id) {
            out.push_back(&e);
        }
    }
    return out;
}

NpcStatsError NpcProfessionTable::parse(const TextTable& table, NpcProfessionTable& out) {
    out.entries_.clear();

    const std::size_t header = find_header(table, kProfName, "Professions");
    if (header == table.row_count()) {
        return NpcStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const int id = table.cell_int(r, kProfId, -1);
        std::string name = cell_text(table, r, kProfName);
        if (id <= 0 || name.empty()) {
            continue;
        }
        NpcProfessionEntry e;
        e.id = id;
        e.name = std::move(name);
        e.random_chance = table.cell_int(r, kProfChance);
        e.hire_cost = table.cell_int(r, kProfCost);
        e.personality = cell_text(table, r, kProfPersonality);
        e.action_text = cell_text(table, r, kProfAction);
        e.party_benefit = cell_text(table, r, kProfBenefit);
        e.join_text = cell_text(table, r, kProfJoinText);
        out.entries_.push_back(std::move(e));
    }
    return NpcStatsError::None;
}

const NpcProfessionEntry* NpcProfessionTable::at(int id) const noexcept {
    for (const auto& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data
