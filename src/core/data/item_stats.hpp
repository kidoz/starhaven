#ifndef STARHAVEN_CORE_DATA_ITEM_STATS_HPP
#define STARHAVEN_CORE_DATA_ITEM_STATS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/dice.hpp"
#include "core/data/text_table.hpp"

namespace starhaven::data {

// Numeric values stored in the executable's compiled ITEMS records. The first
// twelve values also index SPCITEMS columns directly; equipment values 3..11
// index STDITEMS after subtracting three.
enum class ItemEquipType : std::uint8_t {
    Weapon = 0,
    TwoHandedWeapon = 1,
    Missile = 2,
    Armor = 3,
    Shield = 4,
    Helm = 5,
    Belt = 6,
    Cloak = 7,
    Gauntlets = 8,
    Boots = 9,
    Ring = 10,
    Amulet = 11,
    Wand = 12,
    Herb = 13,
    Bottle = 14,
    SpellScroll = 15,
    Book = 16,
    MessageScroll = 17,
    Gold = 18,
    Other = 19,
};

// Numeric values compiled from the `Skill Group` column. `Misc` is the
// executable's fallback for every label outside the named weapon and armour
// skills.
enum class ItemSkillType : std::uint8_t {
    Club = 0,
    Staff = 1,
    Sword = 2,
    Dagger = 3,
    Axe = 4,
    Spear = 5,
    Bow = 6,
    Mace = 7,
    Blaster = 8,
    Shield = 9,
    Leather = 10,
    Chain = 11,
    Plate = 12,
    Misc = 13,
};

[[nodiscard]] ItemEquipType item_equip_type_from_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view item_equip_type_name(ItemEquipType type) noexcept;
[[nodiscard]] ItemSkillType item_skill_type_from_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view item_skill_type_name(ItemSkillType type) noexcept;

// One row of `ITEMS.TXT`. Names follow the shipped column headings where the
// engine meaning has not yet been independently established.
struct ItemStatsEntry {
    int id = 0;  // direct, zero-based item id
    std::string picture;
    std::string name;
    int value = 0;
    std::string equip_stat;
    ItemEquipType equip_type = ItemEquipType::Other;
    std::string skill_group;
    ItemSkillType skill_type = ItemSkillType::Misc;
    std::string modifier_1;
    // **`Mod1`, decoded once.** The original's runtime row keeps a weapon's
    // damage as bytes at `+0x16` — the three weapon-figure getters read them
    // there and add `Mod2` at `+0x18` — so the text is decoded when the table
    // is parsed and never again. This engine used to re-parse `"2d6"` on
    // every roll, every shop listing and every printed range.
    Dice modifier_1_dice;
    int modifier_2 = 0;
    std::string material;  // numeric codes plus the literals "Artifact" and "Relic"
    int id_rep_st = 0;
    std::string unidentified_name;
    int sprite_index = 0;
    int shape = 0;
    int equip_x = 0;
    int equip_y = 0;
    std::string notes;
};

enum class ItemStatsError : std::uint8_t {
    None,
    // No row carries the expected "Item #" / "Pic File" header.
    NoHeader,
    // Item ids are not the contiguous zero-based sequence the binary records
    // index directly.
    BadId,
};

// `ITEMS.TXT`, parsed into direct-id-addressable rows.
class ItemStatsTable {
public:
    ItemStatsTable() = default;

    [[nodiscard]] static ItemStatsError parse(const TextTable& table, ItemStatsTable& out);

    [[nodiscard]] const std::vector<ItemStatsEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Look an item up by the direct zero-based id stored in item instances.
    [[nodiscard]] const ItemStatsEntry* at(std::size_t id) const noexcept;

private:
    std::vector<ItemStatsEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_ITEM_STATS_HPP
