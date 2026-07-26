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

bool read_chance_row(const TextTable& table, std::size_t row,
                     std::array<int, kTreasureLevelCount>& out) {
    for (std::size_t level = 0; level < out.size(); ++level) {
        const int chance = table.cell_int(row, level + 2, -1);
        if (chance < 0 || chance > 100) {
            return false;
        }
        out[level] = chance;
    }
    return true;
}

std::optional<SpecialBonusTreasureClass> parse_special_class(std::string_view text) noexcept {
    text = trim(text);
    if (text.size() != 1) {
        return std::nullopt;
    }
    switch (std::tolower(static_cast<unsigned char>(text.front()))) {
    case 'a':
        return SpecialBonusTreasureClass::A;
    case 'b':
        return SpecialBonusTreasureClass::B;
    case 'c':
        return SpecialBonusTreasureClass::C;
    case 'd':
        return SpecialBonusTreasureClass::D;
    default:
        return std::nullopt;
    }
}

}  // namespace

std::optional<ItemBonusKind> classify_item_bonus(const ItemBonusChances& chances,
                                                 ItemBonusTarget target, std::size_t treasure_level,
                                                 int percentile_roll) noexcept {
    if (treasure_level == 0 || treasure_level > kTreasureLevelCount || percentile_roll < 0 ||
        percentile_roll >= 100) {
        return std::nullopt;
    }

    const std::size_t level = treasure_level - 1;
    if (target == ItemBonusTarget::Weapon) {
        return percentile_roll < chances.weapon_special[level] ? ItemBonusKind::Special
                                                               : ItemBonusKind::None;
    }
    if (target != ItemBonusTarget::Equipment) {
        return ItemBonusKind::None;
    }

    if (percentile_roll < chances.standard[level]) {
        return ItemBonusKind::Standard;
    }
    if (percentile_roll < chances.standard[level] + chances.special[level]) {
        return ItemBonusKind::Special;
    }
    return ItemBonusKind::None;
}

RandomItemError RandomItemTable::parse(const TextTable& table, RandomItemTable& out) {
    out.entries_.clear();
    out.bonus_chances_ = {};

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

    std::size_t bonus_header = table.row_count();
    for (std::size_t row = header + 1; row < table.row_count(); ++row) {
        if (iequals(trim(table.cell(row, 0)), "Bonus chance by level %")) {
            bonus_header = row;
            break;
        }
    }
    if (bonus_header == table.row_count() || bonus_header + 3 >= table.row_count()) {
        out.entries_.clear();
        return RandomItemError::NoBonusChanceHeader;
    }
    for (std::size_t level = 0; level < kTreasureLevelCount; ++level) {
        if (table.cell_int(bonus_header, level + 2, -1) != static_cast<int>(level + 1)) {
            out.entries_.clear();
            return RandomItemError::BadBonusChances;
        }
    }
    if (!iequals(trim(table.cell(bonus_header + 1, 1)), "Standard") ||
        !iequals(trim(table.cell(bonus_header + 2, 1)), "Special") ||
        !iequals(trim(table.cell(bonus_header + 3, 0)), "Weapons") ||
        !iequals(trim(table.cell(bonus_header + 3, 1)), "Special %") ||
        !read_chance_row(table, bonus_header + 1, out.bonus_chances_.standard) ||
        !read_chance_row(table, bonus_header + 2, out.bonus_chances_.special) ||
        !read_chance_row(table, bonus_header + 3, out.bonus_chances_.weapon_special)) {
        out.entries_.clear();
        out.bonus_chances_ = {};
        return RandomItemError::BadBonusChances;
    }
    for (std::size_t level = 0; level < kTreasureLevelCount; ++level) {
        if (out.bonus_chances_.standard[level] + out.bonus_chances_.special[level] > 100) {
            out.entries_.clear();
            out.bonus_chances_ = {};
            return RandomItemError::BadBonusChances;
        }
    }
    return RandomItemError::None;
}

const RandomItemEntry* RandomItemTable::at(std::size_t id) const noexcept {
    return id < entries_.size() ? &entries_[id] : nullptr;
}

int RandomItemTable::total_weight(std::size_t treasure_level) const noexcept {
    if (treasure_level == 0 || treasure_level > kTreasureLevelCount) {
        return 0;
    }
    int total = 0;
    for (std::size_t id = 1; id < entries_.size(); ++id) {
        total += entries_[id].weights[treasure_level - 1];
    }
    return total;
}

const RandomItemEntry* RandomItemTable::select_for_roll(std::size_t treasure_level,
                                                        int roll) const noexcept {
    const int total = total_weight(treasure_level);
    if (total <= 0 || roll < 0 || roll >= total) {
        return nullptr;
    }

    int cumulative = 0;
    for (std::size_t id = 1; id < entries_.size(); ++id) {
        cumulative += entries_[id].weights[treasure_level - 1];
        if (cumulative >= roll) {
            return &entries_[id];
        }
    }
    return nullptr;
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

int StandardBonusTable::total_weight(std::size_t item_type) const noexcept {
    if (item_type >= kStandardBonusItemTypeCount) {
        return 0;
    }
    int total = 0;
    for (const auto& entry : entries_) {
        total += entry.chance_by_item_type[item_type];
    }
    return total;
}

const StandardBonusEntry* StandardBonusTable::select_for_roll(std::size_t item_type,
                                                              int roll) const noexcept {
    const int total = total_weight(item_type);
    if (total <= 0 || roll < 0 || roll >= total) {
        return nullptr;
    }

    int cumulative = 0;
    for (const auto& entry : entries_) {
        cumulative += entry.chance_by_item_type[item_type];
        if (cumulative >= roll) {
            return &entry;
        }
    }
    return nullptr;
}

std::string_view special_bonus_class_name(SpecialBonusTreasureClass treasure_class) noexcept {
    switch (treasure_class) {
    case SpecialBonusTreasureClass::A:
        return "A";
    case SpecialBonusTreasureClass::B:
        return "B";
    case SpecialBonusTreasureClass::C:
        return "C";
    case SpecialBonusTreasureClass::D:
        return "D";
    }
    return {};
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
        const auto treasure_class = parse_special_class(table.cell(row, 15));
        if (!treasure_class) {
            out.entries_.clear();
            return SpecialBonusError::BadTreasureClass;
        }
        entry.treasure_class = *treasure_class;
        entry.description = cell_text(table, row, 16);
        out.entries_.push_back(std::move(entry));
    }
    return SpecialBonusError::None;
}

const SpecialBonusEntry* SpecialBonusTable::at(std::size_t id) const noexcept {
    return id > 0 && id <= entries_.size() ? &entries_[id - 1] : nullptr;
}

bool SpecialBonusTable::eligible(const SpecialBonusEntry& entry,
                                 std::size_t treasure_level) noexcept {
    switch (treasure_level) {
    case 3:
        return entry.treasure_class == SpecialBonusTreasureClass::A ||
               entry.treasure_class == SpecialBonusTreasureClass::B;
    case 4:
        return entry.treasure_class == SpecialBonusTreasureClass::A ||
               entry.treasure_class == SpecialBonusTreasureClass::B ||
               entry.treasure_class == SpecialBonusTreasureClass::C;
    case 5:
        return entry.treasure_class == SpecialBonusTreasureClass::B ||
               entry.treasure_class == SpecialBonusTreasureClass::C ||
               entry.treasure_class == SpecialBonusTreasureClass::D;
    case 6:
        return entry.treasure_class == SpecialBonusTreasureClass::D;
    default:
        return false;
    }
}

int SpecialBonusTable::total_weight(std::size_t item_type,
                                    std::size_t treasure_level) const noexcept {
    if (item_type >= kSpecialBonusItemTypeCount) {
        return 0;
    }
    int total = 0;
    for (const auto& entry : entries_) {
        if (eligible(entry, treasure_level)) {
            total += entry.chance_by_item_type[item_type];
        }
    }
    return total;
}

const SpecialBonusEntry* SpecialBonusTable::select_for_roll(std::size_t item_type,
                                                            std::size_t treasure_level,
                                                            int roll) const noexcept {
    const int total = total_weight(item_type, treasure_level);
    if (total <= 0 || roll < 0 || roll >= total) {
        return nullptr;
    }

    int cumulative = 0;
    const int one_based_roll = roll + 1;
    for (const auto& entry : entries_) {
        if (!eligible(entry, treasure_level)) {
            continue;
        }
        cumulative += entry.chance_by_item_type[item_type];
        if (cumulative >= one_based_roll) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data
