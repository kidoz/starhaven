#ifndef STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP
#define STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

constexpr std::size_t kTreasureLevelCount = 6;
constexpr std::size_t kStandardBonusItemTypeCount = 9;
constexpr std::size_t kSpecialBonusItemTypeCount = 12;

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
};

class RandomItemTable {
public:
    [[nodiscard]] static RandomItemError parse(const TextTable& table, RandomItemTable& out);

    [[nodiscard]] const std::vector<RandomItemEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const RandomItemEntry* at(std::size_t id) const noexcept;

private:
    std::vector<RandomItemEntry> entries_;
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

private:
    std::vector<StandardBonusEntry> entries_;
    std::array<StandardBonusRange, kTreasureLevelCount> ranges_{};
};

// One 1-based selector row of `SPCITEMS.TXT`. Item-type chances are in the
// shipped order: one-handed weapon, two-handed weapon, missile, armor, shield,
// helm, belt, cape, gauntlets, boots, ring, amulet.
struct SpecialBonusEntry {
    int id = 0;
    std::string effect;
    std::string name_affix;
    std::array<int, kSpecialBonusItemTypeCount> chance_by_item_type{};
    std::string value;
    std::string treasure_class;
    std::string description;
};

enum class SpecialBonusError : std::uint8_t {
    None,
    NoHeader,
};

class SpecialBonusTable {
public:
    [[nodiscard]] static SpecialBonusError parse(const TextTable& table, SpecialBonusTable& out);

    [[nodiscard]] const std::vector<SpecialBonusEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const SpecialBonusEntry* at(std::size_t id) const noexcept;

private:
    std::vector<SpecialBonusEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_ITEM_GENERATION_HPP
