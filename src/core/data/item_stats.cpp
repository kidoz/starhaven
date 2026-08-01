#include "core/data/item_stats.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

constexpr std::size_t kColId = 0;
constexpr std::size_t kColPicture = 1;
constexpr std::size_t kColName = 2;
constexpr std::size_t kColValue = 3;
constexpr std::size_t kColEquipStat = 4;
constexpr std::size_t kColSkillGroup = 5;
constexpr std::size_t kColModifier1 = 6;
constexpr std::size_t kColModifier2 = 7;
constexpr std::size_t kColMaterial = 8;
constexpr std::size_t kColIdRepSt = 9;
constexpr std::size_t kColUnidentifiedName = 10;
constexpr std::size_t kColSpriteIndex = 11;
constexpr std::size_t kColShape = 12;
constexpr std::size_t kColEquipX = 13;
constexpr std::size_t kColEquipY = 14;
constexpr std::size_t kColNotes = 15;

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

ItemEquipType item_equip_type_from_name(std::string_view name) noexcept {
    if (iequals(name, "weapon") || iequals(name, "weapon1or2"))
        return ItemEquipType::Weapon;
    if (iequals(name, "weapon2"))
        return ItemEquipType::TwoHandedWeapon;
    if (iequals(name, "missile"))
        return ItemEquipType::Missile;
    if (iequals(name, "armor"))
        return ItemEquipType::Armor;
    if (iequals(name, "shield"))
        return ItemEquipType::Shield;
    if (iequals(name, "helm"))
        return ItemEquipType::Helm;
    if (iequals(name, "belt"))
        return ItemEquipType::Belt;
    if (iequals(name, "cloak"))
        return ItemEquipType::Cloak;
    if (iequals(name, "gauntlets"))
        return ItemEquipType::Gauntlets;
    if (iequals(name, "boots"))
        return ItemEquipType::Boots;
    if (iequals(name, "ring"))
        return ItemEquipType::Ring;
    if (iequals(name, "amulet"))
        return ItemEquipType::Amulet;
    if (iequals(name, "weaponw"))
        return ItemEquipType::Wand;
    if (iequals(name, "herb"))
        return ItemEquipType::Herb;
    if (iequals(name, "bottle"))
        return ItemEquipType::Bottle;
    if (iequals(name, "sscroll"))
        return ItemEquipType::SpellScroll;
    if (iequals(name, "book"))
        return ItemEquipType::Book;
    if (iequals(name, "mscroll"))
        return ItemEquipType::MessageScroll;
    if (iequals(name, "gold"))
        return ItemEquipType::Gold;
    return ItemEquipType::Other;
}

std::string_view item_equip_type_name(ItemEquipType type) noexcept {
    switch (type) {
    case ItemEquipType::Weapon:
        return "weapon";
    case ItemEquipType::TwoHandedWeapon:
        return "two-handed weapon";
    case ItemEquipType::Missile:
        return "missile";
    case ItemEquipType::Armor:
        return "armor";
    case ItemEquipType::Shield:
        return "shield";
    case ItemEquipType::Helm:
        return "helm";
    case ItemEquipType::Belt:
        return "belt";
    case ItemEquipType::Cloak:
        return "cloak";
    case ItemEquipType::Gauntlets:
        return "gauntlets";
    case ItemEquipType::Boots:
        return "boots";
    case ItemEquipType::Ring:
        return "ring";
    case ItemEquipType::Amulet:
        return "amulet";
    case ItemEquipType::Wand:
        return "wand";
    case ItemEquipType::Herb:
        return "herb";
    case ItemEquipType::Bottle:
        return "bottle";
    case ItemEquipType::SpellScroll:
        return "spell scroll";
    case ItemEquipType::Book:
        return "book";
    case ItemEquipType::MessageScroll:
        return "message scroll";
    case ItemEquipType::Gold:
        return "gold";
    case ItemEquipType::Other:
        return "other";
    }
    return {};
}

ItemSkillType item_skill_type_from_name(std::string_view name) noexcept {
    if (iequals(name, "club"))
        return ItemSkillType::Club;
    if (iequals(name, "staff"))
        return ItemSkillType::Staff;
    if (iequals(name, "sword"))
        return ItemSkillType::Sword;
    if (iequals(name, "dagger"))
        return ItemSkillType::Dagger;
    if (iequals(name, "axe"))
        return ItemSkillType::Axe;
    if (iequals(name, "spear"))
        return ItemSkillType::Spear;
    if (iequals(name, "bow"))
        return ItemSkillType::Bow;
    if (iequals(name, "mace"))
        return ItemSkillType::Mace;
    if (iequals(name, "blaster"))
        return ItemSkillType::Blaster;
    if (iequals(name, "shield"))
        return ItemSkillType::Shield;
    if (iequals(name, "leather"))
        return ItemSkillType::Leather;
    if (iequals(name, "chain"))
        return ItemSkillType::Chain;
    if (iequals(name, "plate"))
        return ItemSkillType::Plate;
    return ItemSkillType::Misc;
}

std::string_view item_skill_type_name(ItemSkillType type) noexcept {
    switch (type) {
    case ItemSkillType::Club:
        return "club";
    case ItemSkillType::Staff:
        return "staff";
    case ItemSkillType::Sword:
        return "sword";
    case ItemSkillType::Dagger:
        return "dagger";
    case ItemSkillType::Axe:
        return "axe";
    case ItemSkillType::Spear:
        return "spear";
    case ItemSkillType::Bow:
        return "bow";
    case ItemSkillType::Mace:
        return "mace";
    case ItemSkillType::Blaster:
        return "blaster";
    case ItemSkillType::Shield:
        return "shield";
    case ItemSkillType::Leather:
        return "leather";
    case ItemSkillType::Chain:
        return "chain";
    case ItemSkillType::Plate:
        return "plate";
    case ItemSkillType::Misc:
        return "misc";
    }
    return {};
}

ItemStatsError ItemStatsTable::parse(const TextTable& table, ItemStatsTable& out) {
    out.entries_.clear();

    std::size_t header = table.row_count();
    for (std::size_t row = 0; row < table.row_count(); ++row) {
        if (iequals(trim(table.cell(row, kColId)), "Item #") &&
            iequals(trim(table.cell(row, kColPicture)), "Pic File")) {
            header = row;
            break;
        }
    }
    if (header == table.row_count()) {
        return ItemStatsError::NoHeader;
    }

    for (std::size_t row = header + 1; row < table.row_count(); ++row) {
        const int id = table.cell_int(row, kColId, -1);
        if (id < 0) {
            continue;
        }
        if (static_cast<std::size_t>(id) != out.entries_.size()) {
            out.entries_.clear();
            return ItemStatsError::BadId;
        }

        ItemStatsEntry item;
        item.id = id;
        item.picture = cell_text(table, row, kColPicture);
        item.name = cell_text(table, row, kColName);
        item.value = table.cell_int(row, kColValue);
        item.equip_stat = cell_text(table, row, kColEquipStat);
        item.equip_type = item_equip_type_from_name(item.equip_stat);
        item.skill_group = cell_text(table, row, kColSkillGroup);
        item.skill_type = item_skill_type_from_name(item.skill_group);
        item.modifier_1 = cell_text(table, row, kColModifier1);
        item.modifier_1_dice = parse_dice(item.modifier_1);
        item.modifier_2 = table.cell_int(row, kColModifier2);
        item.material = cell_text(table, row, kColMaterial);
        item.id_rep_st = table.cell_int(row, kColIdRepSt);
        item.unidentified_name = cell_text(table, row, kColUnidentifiedName);
        item.sprite_index = table.cell_int(row, kColSpriteIndex);
        item.shape = table.cell_int(row, kColShape);
        item.equip_x = table.cell_int(row, kColEquipX);
        item.equip_y = table.cell_int(row, kColEquipY);
        item.notes = cell_text(table, row, kColNotes);
        out.entries_.push_back(std::move(item));
    }
    return ItemStatsError::None;
}

const ItemStatsEntry* ItemStatsTable::at(std::size_t id) const noexcept {
    return id < entries_.size() ? &entries_[id] : nullptr;
}

}  // namespace starhaven::data
