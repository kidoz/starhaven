#include "core/data/npc_stats.hpp"

#include <cctype>
#include <cstddef>
#include <map>

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

std::string_view NpcPersonality::message(int number) const noexcept {
    const auto i = static_cast<std::size_t>(number);
    if (number <= 0 || i >= messages.size()) {
        return {};
    }
    return messages[i];
}

NpcStatsError NpcPersonalityTable::parse(const TextTable& table, NpcPersonalityTable& out) {
    out.entries_.clear();
    out.notes_.clear();

    // The header names each personality with a suffix the designers used as a
    // reminder of which approaches work — "Peasant BTB", "Thief BT". The
    // suffix is dropped; the three rows below state the same thing exactly.
    const std::size_t header = find_header(table, 0, "Msg#");
    if (header == table.row_count()) {
        return NpcStatsError::NoHeader;
    }
    constexpr std::size_t kFirstPersonality = 2;
    for (std::size_t c = kFirstPersonality; c < table.rows()[header].size(); ++c) {
        std::string heading = cell_text(table, header, c);
        if (heading.empty()) {
            continue;
        }
        if (const std::size_t space = heading.rfind(' '); space != std::string::npos) {
            heading = heading.substr(0, space);
        }
        NpcPersonality p;
        p.name = std::move(heading);
        out.entries_.push_back(std::move(p));
    }
    if (out.entries_.empty()) {
        return NpcStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const std::string label = cell_text(table, r, 0);
        if (label.empty()) {
            continue;
        }
        // The first three rows are the approach matrix, named rather than
        // numbered; everything after is a numbered message.
        int approach = -1;
        if (iequals(label, "Beg")) {
            approach = static_cast<int>(NpcApproach::Beg);
        } else if (iequals(label, "Bribe")) {
            approach = static_cast<int>(NpcApproach::Bribe);
        } else if (iequals(label, "Threat")) {
            approach = static_cast<int>(NpcApproach::Threat);
        }

        const int number = table.cell_int(r, 0, -1);
        if (approach < 0) {
            if (number <= 0) {
                continue;
            }
            if (static_cast<std::size_t>(number) >= out.notes_.size()) {
                out.notes_.resize(static_cast<std::size_t>(number) + 1);
            }
            out.notes_[static_cast<std::size_t>(number)] = cell_text(table, r, 1);
        }

        for (std::size_t i = 0; i < out.entries_.size(); ++i) {
            const std::size_t column = kFirstPersonality + i;
            if (approach >= 0) {
                out.entries_[i].allows[static_cast<std::size_t>(approach)] =
                    table.cell_int(r, column) != 0;
            } else if (number > 0) {
                auto& messages = out.entries_[i].messages;
                if (static_cast<std::size_t>(number) >= messages.size()) {
                    messages.resize(static_cast<std::size_t>(number) + 1);
                }
                // "n/a" is written where a personality has no such line —
                // always where the message is a refusal it never makes.
                std::string text = cell_text(table, r, column);
                if (!iequals(text, "n/a")) {
                    messages[static_cast<std::size_t>(number)] = std::move(text);
                }
            }
        }
    }
    return NpcStatsError::None;
}

std::string_view NpcPersonalityTable::note(int number) const noexcept {
    const auto i = static_cast<std::size_t>(number);
    if (number <= 0 || i >= notes_.size()) {
        return {};
    }
    return notes_[i];
}

const NpcPersonality* NpcPersonalityTable::find(std::string_view name) const noexcept {
    // An empty name is not a personality. Without this the suffix match below
    // answers with the first entry, since "" ends every string.
    if (name.empty()) {
        return nullptr;
    }
    for (const auto& e : entries_) {
        if (iequals(e.name, name)) {
            return &e;
        }
    }
    // "Evil Fanatic" here against "Fanatic" in the profession table.
    for (const auto& e : entries_) {
        if (e.name.size() > name.size() &&
            iequals(std::string_view(e.name).substr(e.name.size() - name.size()), name)) {
            return &e;
        }
    }
    return nullptr;
}

NpcStatsError NpcDialogueTable::parse(const TextTable& topics, const TextTable& texts,
                                      NpcDialogueTable& out) {
    out.entries_.clear();

    const std::size_t topic_header = find_header(topics, 1, "Topic");
    const std::size_t text_header = find_header(texts, 1, "Text");
    if (topic_header == topics.row_count() || text_header == texts.row_count()) {
        return NpcStatsError::NoHeader;
    }

    std::map<int, std::string> words;
    for (std::size_t r = text_header + 1; r < texts.row_count(); ++r) {
        const int id = texts.cell_int(r, 0, -1);
        std::string text = cell_text(texts, r, 1);
        if (id <= 0 || text.empty()) {
            continue;
        }
        words.emplace(id, std::move(text));
    }

    for (std::size_t r = topic_header + 1; r < topics.row_count(); ++r) {
        const int id = topics.cell_int(r, 0, -1);
        std::string topic = cell_text(topics, r, 1);
        if (id <= 0 || topic.empty()) {
            continue;
        }
        NpcDialogueEntry e;
        e.id = id;
        e.topic = std::move(topic);
        if (const auto it = words.find(id); it != words.end()) {
            e.text = it->second;
        }
        out.entries_.push_back(std::move(e));
    }
    return NpcStatsError::None;
}

const NpcDialogueEntry* NpcDialogueTable::at(int id) const noexcept {
    for (const auto& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

NpcStatsError NpcNewsTable::parse(const TextTable& table, NpcNewsTable& out) {
    out.entries_.clear();

    const std::size_t header = find_header(table, 1, "Map");
    if (header == table.row_count()) {
        return NpcStatsError::NoHeader;
    }
    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const int id = table.cell_int(r, 0, -1);
        std::string text = cell_text(table, r, 3);
        if (id <= 0 || text.empty()) {
            continue;
        }
        NpcNewsEntry e;
        e.id = id;
        e.map_value = table.cell_int(r, 1);
        e.topic = cell_text(table, r, 2);
        e.text = std::move(text);
        out.entries_.push_back(std::move(e));
    }
    return NpcStatsError::None;
}

}  // namespace starhaven::data
