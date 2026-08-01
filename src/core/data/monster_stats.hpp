#ifndef STARHAVEN_CORE_DATA_MONSTER_STATS_HPP
#define STARHAVEN_CORE_DATA_MONSTER_STATS_HPP

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/dice.hpp"
#include "core/data/text_table.hpp"

namespace starhaven::data {

// A resistance value of "Imm" — immune — rather than a number.
constexpr int kResistanceImmune = -1;

// The resistance columns, in table order.
enum class Resistance : std::size_t {
    Fire = 0,
    Electricity,
    Cold,
    Poison,
    Physical,
    Magic,
    Count,
};

constexpr std::size_t kResistanceCount = static_cast<std::size_t>(Resistance::Count);

// One of a monster's two melee/missile attacks. The damage and missile fields
// are the designers' own codes ("2D6+2", "Arrow"); this does not interpret
// them, because what the engine does with them is not yet established.
struct MonsterAttack {
    std::string type;    // "Phys", "Fire", "Elec", ...
    std::string damage;  // dice code, e.g. "2D6+2"
    // **Decoded once, as the runtime row does it.** The 72-byte row holds each
    // attack's damage as bytes — `+0x17`/`+0x18` for the first and
    // `+0x1d`/`+0x1e` for the second — so the text is read when the table is
    // parsed and never again. This engine used to call `parse_dice` twice on
    // every swing.
    Dice damage_dice;
    std::string missile;
    // Whether the `Miss` column names a missile: `"0"` and empty do not.
    bool flies = false;
    int chance = 0;  // percent, from the "Att%" / "Use%" columns
};

// One row of `MONSTERS.TXT`.
struct MonsterStatsEntry {
    int id = 0;           // 1-based row number
    std::string picture;  // matches a DMONLIST.BIN entry name, e.g. "ArcherB"
    std::string name;     // display name, e.g. "Master Archer"

    int level = 0;
    int hit_points = 0;
    int armor_class = 0;
    int experience = 0;

    std::string treasure;  // drop code, e.g. "5%6D20+L2Bow"
    int quest = 0;
    bool flying = false;
    std::string movement;  // "Short", "Med", "Long"
    std::string ai_type;   // "Normal", "Aggress", "Suicidal", ...
    int hostility = 0;
    int speed = 0;
    int recovery = 0;
    std::string preference;
    std::string bonus;  // special on-hit effect, e.g. "BrkItem", "DrainSP"

    std::array<MonsterAttack, 2> attacks;
    std::string spells;     // "Spl,Mas,Skil" column, as written
    int spell_percent = 0;  // how often the spell is used
    std::string special;    // "Misc Special" column

    // Percent resistance, or kResistanceImmune. Index with `Resistance`.
    std::array<int, kResistanceCount> resistances{};

    [[nodiscard]] int resistance(Resistance r) const noexcept {
        return resistances[static_cast<std::size_t>(r)];
    }
};

enum class MonsterStatsError {
    None,
    // No row carries the expected "Picture" / "Name" header.
    NoHeader,
};

// `MONSTERS.TXT`, parsed into rows. The `picture` column is the join to
// `DMONLIST.BIN` (see docs/formats/dmonlist.md), which is what an actor
// record's monster id indexes.
class MonsterStatsTable {
public:
    MonsterStatsTable() = default;

    [[nodiscard]] static MonsterStatsError parse(const TextTable& table, MonsterStatsTable& out);

    [[nodiscard]] const std::vector<MonsterStatsEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Look a monster up by its picture name. Matching ignores case and spaces
    // — see `normalize_picture`. Returns nullptr when the name is not listed.
    [[nodiscard]] const MonsterStatsEntry* find(std::string_view picture) const noexcept;

private:
    std::vector<MonsterStatsEntry> entries_;
};

// Fold a picture name to its comparable form: lowercase, spaces removed.
//
// The text table and `DMONLIST.BIN` disagree on five of the 173 names purely
// in typography — `"DragonCave A"` against `"DragonCaveA"`, `"PeasantF1C"`
// against `"Peasantf1C"` — so an exact comparison would lose those joins for
// no reason. See docs/formats/text-tables.md.
[[nodiscard]] std::string normalize_picture(std::string_view name);

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_MONSTER_STATS_HPP
