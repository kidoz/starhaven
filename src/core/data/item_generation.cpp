#include "core/data/item_generation.hpp"

#include <algorithm>
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

enum class ItemFilterField : std::uint8_t {
    EquipType,
    SkillType,
};

struct ItemFilter {
    ItemFilterField field = ItemFilterField::EquipType;
    std::uint8_t value = 0;
};

ItemFilter item_filter(ItemGenerationType type) noexcept {
    const auto raw = static_cast<std::uint8_t>(type);
    switch (type) {
    case ItemGenerationType::WeaponCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Weapon)};
    case ItemGenerationType::ArmorCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Armor)};
    case ItemGenerationType::Misc:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Misc)};
    case ItemGenerationType::Sword:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Sword)};
    case ItemGenerationType::Dagger:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Dagger)};
    case ItemGenerationType::Axe:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Axe)};
    case ItemGenerationType::Spear:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Spear)};
    case ItemGenerationType::Bow:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Bow)};
    case ItemGenerationType::Mace:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Mace)};
    case ItemGenerationType::Club:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Club)};
    case ItemGenerationType::Staff:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Staff)};
    case ItemGenerationType::Leather:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Leather)};
    case ItemGenerationType::Chain:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Chain)};
    case ItemGenerationType::Plate:
        return {ItemFilterField::SkillType, static_cast<std::uint8_t>(ItemSkillType::Plate)};
    case ItemGenerationType::ShieldCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Shield)};
    case ItemGenerationType::HelmCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Helm)};
    case ItemGenerationType::BeltCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Belt)};
    case ItemGenerationType::CloakCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Cloak)};
    case ItemGenerationType::GauntletsCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Gauntlets)};
    case ItemGenerationType::BootsCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Boots)};
    case ItemGenerationType::RingCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Ring)};
    case ItemGenerationType::AmuletCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Amulet)};
    case ItemGenerationType::WandCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::Wand)};
    case ItemGenerationType::SpellScrollCategory:
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(ItemEquipType::SpellScroll)};
    default:
        // This is also the executable's fallback for nonzero values outside
        // its 20..43 alias switch.
        return {ItemFilterField::EquipType, static_cast<std::uint8_t>(raw - 1)};
    }
}

bool matches_filter(const ItemStatsEntry& item, ItemFilter filter) noexcept {
    if (filter.field == ItemFilterField::SkillType) {
        return static_cast<std::uint8_t>(item.skill_type) == filter.value;
    }
    return static_cast<std::uint8_t>(item.equip_type) == filter.value;
}

}  // namespace

std::string_view item_generation_type_name(ItemGenerationType type) noexcept {
    switch (type) {
    case ItemGenerationType::Any:
        return "any";
    case ItemGenerationType::Weapon:
        return "weapon";
    case ItemGenerationType::TwoHandedWeapon:
        return "two-handed weapon";
    case ItemGenerationType::Missile:
        return "missile";
    case ItemGenerationType::Armor:
        return "armor";
    case ItemGenerationType::Shield:
        return "shield";
    case ItemGenerationType::Helm:
        return "helm";
    case ItemGenerationType::Belt:
        return "belt";
    case ItemGenerationType::Cloak:
        return "cloak";
    case ItemGenerationType::Gauntlets:
        return "gauntlets";
    case ItemGenerationType::Boots:
        return "boots";
    case ItemGenerationType::Ring:
        return "ring";
    case ItemGenerationType::Amulet:
        return "amulet";
    case ItemGenerationType::Wand:
        return "wand";
    case ItemGenerationType::Reagent:
        return "reagent";
    case ItemGenerationType::Potion:
        return "potion";
    case ItemGenerationType::SpellScroll:
        return "spell scroll";
    case ItemGenerationType::Book:
        return "book";
    case ItemGenerationType::MessageScroll:
        return "message scroll";
    case ItemGenerationType::Gold:
        return "gold";
    case ItemGenerationType::WeaponCategory:
        return "weapon category";
    case ItemGenerationType::ArmorCategory:
        return "armor category";
    case ItemGenerationType::Misc:
        return "misc";
    case ItemGenerationType::Sword:
        return "sword";
    case ItemGenerationType::Dagger:
        return "dagger";
    case ItemGenerationType::Axe:
        return "axe";
    case ItemGenerationType::Spear:
        return "spear";
    case ItemGenerationType::Bow:
        return "bow";
    case ItemGenerationType::Mace:
        return "mace";
    case ItemGenerationType::Club:
        return "club";
    case ItemGenerationType::Staff:
        return "staff";
    case ItemGenerationType::Leather:
        return "leather";
    case ItemGenerationType::Chain:
        return "chain";
    case ItemGenerationType::Plate:
        return "plate";
    case ItemGenerationType::ShieldCategory:
        return "shield category";
    case ItemGenerationType::HelmCategory:
        return "helm category";
    case ItemGenerationType::BeltCategory:
        return "belt category";
    case ItemGenerationType::CloakCategory:
        return "cloak category";
    case ItemGenerationType::GauntletsCategory:
        return "gauntlets category";
    case ItemGenerationType::BootsCategory:
        return "boots category";
    case ItemGenerationType::RingCategory:
        return "ring category";
    case ItemGenerationType::AmuletCategory:
        return "amulet category";
    case ItemGenerationType::WandCategory:
        return "wand category";
    case ItemGenerationType::SpellScrollCategory:
        return "spell scroll category";
    }
    return "unknown";
}

std::optional<ChestTreasureLevelRange>
chest_treasure_level_range(std::size_t placeholder_class, std::size_t map_treasure_class) noexcept {
    constexpr std::array<std::array<ChestTreasureLevelRange, kMapTreasureClassCount>,
                         kChestTreasureClassCount>
        kRanges{{
            {{{1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}}},
            {{{1, 1}, {1, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}}},
            {{{1, 2}, {2, 2}, {2, 3}, {3, 3}, {3, 3}, {3, 3}, {3, 3}}},
            {{{2, 2}, {2, 3}, {3, 3}, {3, 4}, {4, 4}, {4, 4}, {4, 4}}},
            {{{2, 3}, {3, 3}, {3, 4}, {4, 4}, {4, 5}, {5, 5}, {5, 5}}},
            {{{3, 3}, {3, 4}, {4, 4}, {4, 5}, {5, 5}, {5, 6}, {6, 6}}},
        }};

    if (placeholder_class == 0 || placeholder_class > kRanges.size() ||
        map_treasure_class >= kRanges.front().size()) {
        return std::nullopt;
    }
    return kRanges[placeholder_class - 1][map_treasure_class];
}

std::optional<int> roll_chest_treasure_level(std::size_t placeholder_class,
                                             std::size_t map_treasure_class,
                                             Mm6Random& random) noexcept {
    const auto range = chest_treasure_level_range(placeholder_class, map_treasure_class);
    if (!range.has_value()) {
        return std::nullopt;
    }
    const auto width = static_cast<std::uint32_t>(range->maximum - range->minimum + 1);
    return range->minimum + static_cast<int>(random.next() % width);
}

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

ItemGenerationError generate_random_item(const RandomItemTable& random_items,
                                         const ItemStatsTable& items,
                                         const StandardBonusTable& standard_bonuses,
                                         const SpecialBonusTable& special_bonuses,
                                         std::size_t treasure_level, Mm6Random& random,
                                         ArtifactGenerationState& artifacts,
                                         GeneratedItem& out) noexcept {
    return generate_random_item(random_items, items, standard_bonuses, special_bonuses,
                                treasure_level, ItemGenerationType::Any, random, artifacts, out);
}

ItemGenerationError generate_random_item(const RandomItemTable& random_items,
                                         const ItemStatsTable& items,
                                         const StandardBonusTable& standard_bonuses,
                                         const SpecialBonusTable& special_bonuses,
                                         std::size_t treasure_level, ItemGenerationType type,
                                         Mm6Random& random, ArtifactGenerationState& artifacts,
                                         GeneratedItem& out) noexcept {
    out = {};
    if (treasure_level == 0 || treasure_level > kTreasureLevelCount) {
        return ItemGenerationError::BadTreasureLevel;
    }

    const RandomItemEntry* base = nullptr;
    if (type == ItemGenerationType::Any) {
        const int base_total = random_items.total_weight(treasure_level);
        if (base_total <= 0) {
            return ItemGenerationError::NoBaseWeight;
        }

        // The candidate draw occurs at every level even though only level 6
        // can award an artifact. This otherwise-unused call is part of MM6's
        // unrestricted sequence.
        const std::size_t artifact_candidate = random.next() % kArtifactCandidateCount;
        if (treasure_level == 6) {
            const int artifact_roll = random.next() % 100;
            const auto found_count = static_cast<std::size_t>(
                std::count(artifacts.found.begin(), artifacts.found.end(), true));
            if (artifact_roll < 5 && !artifacts.found[artifact_candidate] &&
                found_count < kMaximumGeneratedArtifacts) {
                artifacts.found[artifact_candidate] = true;
                out.item_id = static_cast<int>(400 + artifact_candidate);
                return ItemGenerationError::None;
            }
        }

        base = random_items.select_for_roll(treasure_level, random.next() % base_total);
    } else {
        const ItemFilter filter = item_filter(type);
        const RandomItemEntry* first_match = nullptr;
        int total = 0;
        for (std::size_t id = 1; id < random_items.size(); ++id) {
            const auto* candidate = random_items.at(id);
            const auto* candidate_stats = items.at(id);
            if (candidate == nullptr || candidate_stats == nullptr) {
                return ItemGenerationError::MissingItemStats;
            }
            if (!matches_filter(*candidate_stats, filter)) {
                continue;
            }
            if (first_match == nullptr) {
                first_match = candidate;
            }
            total += candidate->weights[treasure_level - 1];
        }

        // MM6 keeps matching zero-weight entries in its candidate array. When
        // their total is zero it consumes no selector call and chooses the
        // first match; no matches leave the zero-initialized item id 0.
        if (total <= 0) {
            base = first_match != nullptr ? first_match : random_items.at(0);
        } else {
            const int roll = random.next() % total;
            int cumulative = 0;
            for (std::size_t id = 1; id < random_items.size(); ++id) {
                const auto* candidate = random_items.at(id);
                const auto* candidate_stats = items.at(id);
                if (candidate == nullptr || candidate_stats == nullptr ||
                    !matches_filter(*candidate_stats, filter)) {
                    continue;
                }
                cumulative += candidate->weights[treasure_level - 1];
                if (cumulative >= roll) {
                    base = candidate;
                    break;
                }
            }
        }
    }

    if (base == nullptr) {
        return ItemGenerationError::NoBaseWeight;
    }
    const auto* item = items.at(static_cast<std::size_t>(base->id));
    if (item == nullptr) {
        return ItemGenerationError::MissingItemStats;
    }

    out.item_id = base->id;
    out.identified = item->id_rep_st == 0;

    const auto equip_type = static_cast<std::size_t>(item->equip_type);
    const auto select_special = [&]() -> ItemGenerationError {
        const int total = special_bonuses.total_weight(equip_type, treasure_level);
        if (total <= 0) {
            return ItemGenerationError::NoSpecialWeight;
        }
        const auto* bonus =
            special_bonuses.select_for_roll(equip_type, treasure_level, random.next() % total);
        if (bonus == nullptr) {
            return ItemGenerationError::NoSpecialWeight;
        }
        out.special_bonus = bonus->id;
        return ItemGenerationError::None;
    };

    const auto& chances = random_items.bonus_chances();
    const std::size_t level_index = treasure_level - 1;
    if (equip_type <= static_cast<std::size_t>(ItemEquipType::Missile)) {
        const int special_chance = chances.weapon_special[level_index];
        if (special_chance != 0 && random.next() % 100 < special_chance) {
            return select_special();
        }
        return ItemGenerationError::None;
    }

    if (equip_type >= static_cast<std::size_t>(ItemEquipType::Armor) &&
        equip_type <= static_cast<std::size_t>(ItemEquipType::Amulet)) {
        const int standard_chance = chances.standard[level_index];
        if (standard_chance == 0) {
            return ItemGenerationError::None;
        }

        const int bonus_roll = random.next() % 100;
        if (bonus_roll >= standard_chance) {
            if (bonus_roll < standard_chance + chances.special[level_index]) {
                return select_special();
            }
            return ItemGenerationError::None;
        }

        const std::size_t standard_type =
            equip_type - static_cast<std::size_t>(ItemEquipType::Armor);
        const int total = standard_bonuses.total_weight(standard_type);
        if (total <= 0) {
            return ItemGenerationError::NoStandardWeight;
        }
        const auto* bonus = standard_bonuses.select_for_roll(standard_type, random.next() % total);
        if (bonus == nullptr) {
            return ItemGenerationError::NoStandardWeight;
        }
        const auto* range = standard_bonuses.range(treasure_level);
        if (range == nullptr || range->maximum < range->minimum) {
            return ItemGenerationError::BadStandardRange;
        }
        const int width = range->maximum - range->minimum + 1;
        out.standard_bonus = bonus->id;
        out.standard_bonus_strength = range->minimum + random.next() % width;
        return ItemGenerationError::None;
    }

    if (item->equip_type == ItemEquipType::Wand) {
        out.charges = item->modifier_2 + random.next() % 6;
    }
    return ItemGenerationError::None;
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

// Which class letters a treasure level may draw a special bonus from.
//
// **This ladder is this engine's own.** The percentile split above it — how
// often a standard bonus, a special one or neither appears at each level —
// is read from `RNDITEMS.TXT` and is the table's; only this last step is
// invented. It was hunted in the executable and not found: no bitmask of
// four class bits per level and no low/high pair per level sits in either
// data section, and the generator routine that would hold the comparisons
// inline was not located. `inferred`
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
