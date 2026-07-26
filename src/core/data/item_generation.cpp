#include "core/data/item_generation.hpp"

#include <cctype>
#include <utility>

namespace starhaven::data {

namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

std::string cell_text(const TextTable& table, std::size_t row, std::size_t column) {
    return std::string(trim(table.cell(row, column)));
}

}  // namespace

RandomItemError RandomItemTable::parse(const TextTable& table, RandomItemTable& out) {
    out.entries_.clear();

    std::size_t header = table.row_count();
    for (std::size_t row = 0; row < table.row_count(); ++row) {
        if (iequals(trim(table.cell(row, 0)), "Item #") &&
            iequals(trim(table.cell(row, 1)), "Pic File")) {
            header = row;
            break;
        }
    }
    if (header == table.row_count()) {
        return RandomItemError::NoHeader;
    }

    for (std::size_t row = header + 1; row < table.row_count(); ++row) {
        const int id = table.cell_int(row, 0, -1);
        if (id < 0) {
            continue;
        }
        if (static_cast<std::size_t>(id) != out.entries_.size()) {
            out.entries_.clear();
            return RandomItemError::BadId;
        }

        RandomItemEntry entry;
        entry.id = id;
        entry.picture = cell_text(table, row, 1);
        for (std::size_t level = 0; level < entry.weights.size(); ++level) {
            entry.weights[level] = table.cell_int(row, 2 + level);
        }
        out.entries_.push_back(std::move(entry));
    }
    return RandomItemError::None;
}

const RandomItemEntry* RandomItemTable::at(std::size_t id) const noexcept {
    return id < entries_.size() ? &entries_[id] : nullptr;
}

StandardBonusError StandardBonusTable::parse(const TextTable& table, StandardBonusTable& out) {
    out.entries_.clear();
    out.ranges_.fill({});

    std::size_t bonus_header = table.row_count();
    std::size_t range_header = table.row_count();
    for (std::size_t row = 0; row < table.row_count(); ++row) {
        if (iequals(trim(table.cell(row, 0)), "Bonus Stat") &&
            iequals(trim(table.cell(row, 1)), "Of Name")) {
            bonus_header = row;
        }
        if (iequals(trim(table.cell(row, 1)), "lvl") && iequals(trim(table.cell(row, 2)), "min") &&
            iequals(trim(table.cell(row, 3)), "max")) {
            range_header = row;
        }
    }
    if (bonus_header == table.row_count() || range_header == table.row_count()) {
        return StandardBonusError::NoHeader;
    }

    for (std::size_t row = bonus_header + 1; row < range_header; ++row) {
        std::string stat = cell_text(table, row, 0);
        if (stat.empty()) {
            continue;
        }
        StandardBonusEntry entry;
        entry.id = static_cast<int>(out.entries_.size() + 1);
        entry.stat = std::move(stat);
        entry.name_suffix = cell_text(table, row, 1);
        for (std::size_t type = 0; type < entry.chance_by_item_type.size(); ++type) {
            entry.chance_by_item_type[type] = table.cell_int(row, 2 + type);
        }
        out.entries_.push_back(std::move(entry));
    }

    std::size_t expected_level = 1;
    for (std::size_t row = range_header + 1; row < table.row_count(); ++row) {
        const int level = table.cell_int(row, 1, -1);
        if (level < 0) {
            continue;
        }
        if (!std::cmp_equal(level, expected_level) || expected_level > kTreasureLevelCount) {
            out.entries_.clear();
            out.ranges_.fill({});
            return StandardBonusError::BadLevel;
        }
        out.ranges_[expected_level - 1] = {
            table.cell_int(row, 2),
            table.cell_int(row, 3),
        };
        ++expected_level;
    }
    if (expected_level != kTreasureLevelCount + 1) {
        out.entries_.clear();
        out.ranges_.fill({});
        return StandardBonusError::BadLevel;
    }
    return StandardBonusError::None;
}

const StandardBonusEntry* StandardBonusTable::at(std::size_t id) const noexcept {
    return id > 0 && id <= entries_.size() ? &entries_[id - 1] : nullptr;
}

const StandardBonusRange* StandardBonusTable::range(std::size_t treasure_level) const noexcept {
    return treasure_level > 0 && treasure_level <= ranges_.size() ? &ranges_[treasure_level - 1]
                                                                  : nullptr;
}

SpecialBonusError SpecialBonusTable::parse(const TextTable& table, SpecialBonusTable& out) {
    out.entries_.clear();

    std::size_t header = table.row_count();
    for (std::size_t row = 0; row < table.row_count(); ++row) {
        if (iequals(trim(table.cell(row, 0)), "Bonus Stat") &&
            iequals(trim(table.cell(row, 1)), "Name Add")) {
            header = row;
            break;
        }
    }
    if (header == table.row_count()) {
        return SpecialBonusError::NoHeader;
    }

    bool have_entries = false;
    for (std::size_t row = header + 1; row < table.row_count(); ++row) {
        std::string effect = cell_text(table, row, 0);
        if (effect.empty()) {
            if (have_entries) {
                break;
            }
            continue;
        }
        have_entries = true;

        SpecialBonusEntry entry;
        entry.id = static_cast<int>(out.entries_.size() + 1);
        entry.effect = std::move(effect);
        entry.name_affix = cell_text(table, row, 1);
        for (std::size_t type = 0; type < entry.chance_by_item_type.size(); ++type) {
            entry.chance_by_item_type[type] = table.cell_int(row, 2 + type);
        }
        entry.value = cell_text(table, row, 14);
        entry.treasure_class = cell_text(table, row, 15);
        entry.description = cell_text(table, row, 16);
        out.entries_.push_back(std::move(entry));
    }
    return SpecialBonusError::None;
}

const SpecialBonusEntry* SpecialBonusTable::at(std::size_t id) const noexcept {
    return id > 0 && id <= entries_.size() ? &entries_[id - 1] : nullptr;
}

}  // namespace starhaven::data
