#include "core/data/spell_stats.hpp"

#include <array>
#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

// Column indices, the same for every school section.
constexpr std::size_t kColId = 0;
constexpr std::size_t kColNumber = 1;  // and, on a heading row, the school
constexpr std::size_t kColName = 2;
constexpr std::size_t kColElement = 3;
constexpr std::size_t kColShortName = 4;
constexpr std::size_t kColCostNormal = 5;
constexpr std::size_t kColCostExpert = 6;
constexpr std::size_t kColCostMaster = 7;
constexpr std::size_t kColDescription = 8;
constexpr std::size_t kColNormal = 9;
constexpr std::size_t kColExpert = 10;
constexpr std::size_t kColMaster = 11;

constexpr std::array<std::string_view, kSpellSchoolCount> kSchoolNames{
    "Fire", "Air", "Water", "Earth", "Spirit", "Mind", "Body", "Light", "Dark"};

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

std::string lowercase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

// A heading row names its school in the column the spell rows use for the
// number within the school: "Fire Spells", "Dark Spells".
bool school_heading(std::string_view cell, SpellSchool& out) {
    constexpr std::string_view suffix = " Spells";
    if (cell.size() <= suffix.size() ||
        !iequals(cell.substr(cell.size() - suffix.size()), suffix)) {
        return false;
    }
    const std::string_view name = cell.substr(0, cell.size() - suffix.size());
    for (std::size_t i = 0; i < kSchoolNames.size(); ++i) {
        if (iequals(name, kSchoolNames[i])) {
            out = static_cast<SpellSchool>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

std::string_view school_name(SpellSchool school) noexcept {
    const auto i = static_cast<std::size_t>(school);
    return i < kSchoolNames.size() ? kSchoolNames[i] : std::string_view{};
}

SpellStatsError SpellStatsTable::parse(const TextTable& table, SpellStatsTable& out) {
    out.entries_.clear();
    out.by_name_.clear();

    bool have_school = false;
    SpellSchool school = SpellSchool::Fire;

    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (SpellSchool heading = SpellSchool::Fire;
            school_heading(trim(table.cell(r, kColNumber)), heading)) {
            school = heading;
            have_school = true;
            continue;
        }
        if (!have_school) {
            continue;
        }
        const int id = table.cell_int(r, kColId, -1);
        std::string name = cell_text(table, r, kColName);
        if (id <= 0 || name.empty()) {
            continue;
        }

        SpellStatsEntry e;
        e.id = id;
        e.school = school;
        e.number = table.cell_int(r, kColNumber);
        e.name = std::move(name);
        e.element = cell_text(table, r, kColElement);
        e.short_name = cell_text(table, r, kColShortName);
        e.cost_normal = table.cell_int(r, kColCostNormal);
        e.cost_expert = table.cell_int(r, kColCostExpert);
        e.cost_master = table.cell_int(r, kColCostMaster);
        e.description = cell_text(table, r, kColDescription);
        e.normal = cell_text(table, r, kColNormal);
        e.expert = cell_text(table, r, kColExpert);
        e.master = cell_text(table, r, kColMaster);

        out.by_name_.emplace(lowercase(e.name), out.entries_.size());
        out.entries_.push_back(std::move(e));
    }

    if (!have_school) {
        return SpellStatsError::NoSchools;
    }
    return SpellStatsError::None;
}

const SpellStatsEntry* SpellStatsTable::at(int id) const noexcept {
    for (const auto& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const SpellStatsEntry* SpellStatsTable::find(std::string_view name) const noexcept {
    const auto it = by_name_.find(lowercase(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

void DescriptionTable::parse(const TextTable& table, DescriptionTable& out) {
    out.entries_.clear();
    // All three of these tables open with a heading row whose columns are the
    // labels — "Class / Descriptions", "Skill / Description / Normal / …" — and
    // it is otherwise shaped exactly like a data row, so it is skipped by
    // position rather than by inspecting it.
    bool heading_seen = false;
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        std::string name = cell_text(table, r, 0);
        if (name.empty()) {
            continue;
        }
        if (!heading_seen) {
            heading_seen = true;
            continue;
        }
        DescribedEntry e;
        e.name = std::move(name);
        for (std::size_t c = 1; c < table.rows()[r].size(); ++c) {
            std::string text = cell_text(table, r, c);
            if (!text.empty()) {
                e.text.push_back(std::move(text));
            }
        }
        // The heading row has a name but no prose; so does nothing else here.
        if (e.text.empty()) {
            continue;
        }
        out.entries_.push_back(std::move(e));
    }
}

const DescribedEntry* DescriptionTable::find(std::string_view name) const noexcept {
    for (const auto& e : entries_) {
        if (iequals(e.name, name)) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data
