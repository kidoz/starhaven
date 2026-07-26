#ifndef STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP
#define STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

constexpr std::size_t kTreasureLevelCount = 6;
constexpr std::size_t kStandardBonusItemTypeCount = 9;
constexpr std::size_t kSpecialBonusItemTypeCount = 12;

enum class ItemBonusKind : std::uint8_t {
    None,
    Standard,
    Special,
};

enum class ItemBonusTarget : std::uint8_t {
    Weapon,
    Equipment,
    Other,
};

struct ItemBonusChances {
    std::array<int, kTreasureLevelCount> standard{};
    std::array<int, kTreasureLevelCount> special{};
    std::array<int, kTreasureLevelCount> weapon_special{};
};

// Resolve the generator's percentile branch. Treasure levels are 1..6 and
// percentile rolls are 0..99; invalid inputs return std::nullopt.
[[nodiscard]] std::optional<ItemBonusKind> classify_item_bonus(const ItemBonusChances& chances,
                                                               ItemBonusTarget target,
                                                               std::size_t treasure_level,
                                                               int percentile_roll) noexcept;

// One direct-id row of `RNDITEMS.TXT`.
struct RandomItemEntry {
    int id = 0;
    std::string picture;
    std::array<int, kTreasureLevelCount> weights{};
};

enum class RandomItemError : std::uint8_t {
    None,
    NoHeader,
    BadId,
    NoBonusChanceHeader,
    BadBonusChances,
};

class RandomItemTable {
public:
    [[nodiscard]] static RandomItemError parse(const TextTable& table, RandomItemTable& out);

    [[nodiscard]] const std::vector<RandomItemEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const RandomItemEntry* at(std::size_t id) const noexcept;
    [[nodiscard]] const ItemBonusChances& bonus_chances() const noexcept { return bonus_chances_; }

    // Treasure levels are 1..6. Selection excludes empty id 0 and accepts the
    // engine remainder in 0..total_weight-1. The inclusive comparison used by
    // MM6 gives the first candidate one extra outcome; a zero-weight first row
    // can therefore win roll zero.
    [[nodiscard]] int total_weight(std::size_t treasure_level) const noexcept;
    [[nodiscard]] const RandomItemEntry* select_for_roll(std::size_t treasure_level,
                                                         int roll) const noexcept;

private:
    std::vector<RandomItemEntry> entries_;
    ItemBonusChances bonus_chances_;
};

struct StandardBonusRange {
    int minimum = 0;
    int maximum = 0;
};

// One 1-based selector row of `STDITEMS.TXT`. Item-type chances are in the
// shipped order: armor, shield, helm, belt, cape, gauntlets, boots, ring,
// amulet.
struct StandardBonusEntry {
    int id = 0;
    std::string stat;
    std::string name_suffix;
    std::array<int, kStandardBonusItemTypeCount> chance_by_item_type{};
};

enum class StandardBonusError : std::uint8_t {
    None,
    NoHeader,
    BadLevel,
};

class StandardBonusTable {
public:
    [[nodiscard]] static StandardBonusError parse(const TextTable& table, StandardBonusTable& out);

    [[nodiscard]] const std::vector<StandardBonusEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const StandardBonusEntry* at(std::size_t id) const noexcept;

    // Treasure levels are 1..6. Returns nullptr outside that range.
    [[nodiscard]] const StandardBonusRange* range(std::size_t treasure_level) const noexcept;

    // Item-type indices follow the nine STDITEMS columns. Selection accepts
    // the engine remainder in 0..total_weight-1 and preserves MM6's inclusive
    // comparison quirk, including a possible zero-weight first-row win.
    [[nodiscard]] int total_weight(std::size_t item_type) const noexcept;
    [[nodiscard]] const StandardBonusEntry* select_for_roll(std::size_t item_type,
                                                            int roll) const noexcept;

private:
    std::vector<StandardBonusEntry> entries_;
    std::array<StandardBonusRange, kTreasureLevelCount> ranges_{};
};

enum class SpecialBonusTreasureClass : std::uint8_t {
    A,
    B,
    C,
    D,
};

[[nodiscard]] std::string_view
special_bonus_class_name(SpecialBonusTreasureClass treasure_class) noexcept;

// One 1-based selector row of `SPCITEMS.TXT`. Item-type chances are in the
// shipped order: one-handed weapon, two-handed weapon, missile, armor, shield,
// helm, belt, cape, gauntlets, boots, ring, amulet.
struct SpecialBonusEntry {
    int id = 0;
    std::string effect;
    std::string name_affix;
    std::array<int, kSpecialBonusItemTypeCount> chance_by_item_type{};
    std::string value;
    SpecialBonusTreasureClass treasure_class = SpecialBonusTreasureClass::A;
    std::string description;
};

enum class SpecialBonusError : std::uint8_t {
    None,
    NoHeader,
    BadTreasureClass,
};

class SpecialBonusTable {
public:
    [[nodiscard]] static SpecialBonusError parse(const TextTable& table, SpecialBonusTable& out);

    [[nodiscard]] const std::vector<SpecialBonusEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const SpecialBonusEntry* at(std::size_t id) const noexcept;

    // Item-type indices follow the twelve SPCITEMS columns. Only treasure
    // classes eligible at the requested level contribute to selection.
    [[nodiscard]] static bool eligible(const SpecialBonusEntry& entry,
                                       std::size_t treasure_level) noexcept;
    [[nodiscard]] int total_weight(std::size_t item_type,
                                   std::size_t treasure_level) const noexcept;
    [[nodiscard]] const SpecialBonusEntry*
    select_for_roll(std::size_t item_type, std::size_t treasure_level, int roll) const noexcept;

private:
    std::vector<SpecialBonusEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP
