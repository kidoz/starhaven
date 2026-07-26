#ifndef STARHAVEN_CORE_DATA_SPELL_STATS_HPP
#define STARHAVEN_CORE_DATA_SPELL_STATS_HPP

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// The nine schools, in the order `Spells.txt` lists them.
enum class SpellSchool : std::uint8_t {
    Fire,
    Air,
    Water,
    Earth,
    Spirit,
    Mind,
    Body,
    Light,
    Dark,
    Count,
};

constexpr std::size_t kSpellSchoolCount = static_cast<std::size_t>(SpellSchool::Count);

// How many spells each school has. Nine schools of eleven is the whole table.
constexpr int kSpellsPerSchool = 11;

[[nodiscard]] std::string_view school_name(SpellSchool school) noexcept;

// One row of `Spells.txt`.
struct SpellStatsEntry {
    int id = 0;  // 1..99, unique across all schools
    SpellSchool school = SpellSchool::Fire;
    int number = 0;  // 1..11 within the school

    std::string name;
    std::string short_name;
    std::string element;  // "Fire", "Elec", "none" — what it is resisted as

    // Spell point cost at each mastery. The table heads these `A`, `X`, `M`,
    // and never lets a higher mastery cost more: across all 99 rows the three
    // are non-increasing, and 94 rows have them equal. `inferred`
    int cost_normal = 0;
    int cost_expert = 0;
    int cost_master = 0;

    std::string description;
    std::string normal;  // what the spell does at each mastery
    std::string expert;
    std::string master;
};

enum class SpellStatsError : std::uint8_t {
    None,
    // No row carries a school heading, so the table is not `Spells.txt`.
    NoSchools,
};

// `Spells.txt`, parsed into rows.
//
// The table is nine sections, each introduced by a heading row naming the
// school — "Fire Spells", "Air Spells" — and followed by its eleven spells.
// The school is not repeated on the spell rows, so it has to be carried down
// from the heading.
class SpellStatsTable {
public:
    SpellStatsTable() = default;

    [[nodiscard]] static SpellStatsError parse(const TextTable& table, SpellStatsTable& out);

    [[nodiscard]] const std::vector<SpellStatsEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Resolve by the id the table gives, 1..99. Returns nullptr when absent.
    [[nodiscard]] const SpellStatsEntry* at(int id) const noexcept;

    // Resolve by name, ignoring case. `MONSTERS.TXT` names spells this way in
    // its spell column, which is the join this table exists for.
    [[nodiscard]] const SpellStatsEntry* find(std::string_view name) const noexcept;

private:
    std::vector<SpellStatsEntry> entries_;
    std::map<std::string, std::size_t, std::less<>> by_name_;
};

// A table of names and prose: `Class.txt`, `stats.txt`, `SkillDes.txt`. All
// three are a heading row and then one row per thing, with the name in the
// first column and one or more description columns after it.
struct DescribedEntry {
    std::string name;
    std::vector<std::string> text;  // the description columns, in table order
};

class DescriptionTable {
public:
    DescriptionTable() = default;

    // Never fails: a table with no usable rows simply comes back empty, since
    // these carry no structure that could be violated.
    static void parse(const TextTable& table, DescriptionTable& out);

    [[nodiscard]] const std::vector<DescribedEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const DescribedEntry* find(std::string_view name) const noexcept;

private:
    std::vector<DescribedEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_SPELL_STATS_HPP
