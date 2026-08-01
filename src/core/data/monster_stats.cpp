#include "core/data/monster_stats.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

constexpr std::size_t kColId = 0;
constexpr std::size_t kColPicture = 1;
constexpr std::size_t kColName = 2;
constexpr std::size_t kColLevel = 3;
constexpr std::size_t kColHitPoints = 4;
constexpr std::size_t kColArmorClass = 5;
constexpr std::size_t kColExperience = 6;
constexpr std::size_t kColTreasure = 7;
constexpr std::size_t kColQuest = 8;
constexpr std::size_t kColFly = 9;
constexpr std::size_t kColMovement = 10;
constexpr std::size_t kColAiType = 11;
constexpr std::size_t kColHostility = 12;
constexpr std::size_t kColSpeed = 13;
constexpr std::size_t kColRecovery = 14;
constexpr std::size_t kColPreference = 15;
constexpr std::size_t kColBonus = 16;
constexpr std::size_t kColAttacks = 17;  // two slots of four columns
constexpr std::size_t kAttackStride = 4;
constexpr std::size_t kColSpellPercent = 24;
constexpr std::size_t kColSpells = 25;
constexpr std::size_t kColResistances = 26;  // six columns
constexpr std::size_t kColSpecial = 32;

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

}  // namespace

std::string normalize_picture(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (c == ' ')
            continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

namespace {

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

int resistance_value(std::string_view text) noexcept {
    const std::string_view v = trim(text);
    if (iequals(v, "Imm")) {
        return kResistanceImmune;
    }
    return parse_int(v);
}

}  // namespace

MonsterStatsError MonsterStatsTable::parse(const TextTable& table, MonsterStatsTable& out) {
    out.entries_.clear();

    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, kColPicture)), "Picture") &&
            iequals(trim(table.cell(r, kColName)), "Name")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return MonsterStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        // Two rows of defaults follow the header, carrying no id. A row is a
        // monster only when it has both an id and a picture name.
        const int id = table.cell_int(r, kColId, -1);
        std::string picture = cell_text(table, r, kColPicture);
        if (id <= 0 || picture.empty()) {
            continue;
        }

        MonsterStatsEntry e;
        e.id = id;
        e.picture = std::move(picture);
        e.name = cell_text(table, r, kColName);
        e.level = table.cell_int(r, kColLevel);
        e.hit_points = table.cell_int(r, kColHitPoints);
        e.armor_class = table.cell_int(r, kColArmorClass);
        e.experience = table.cell_int(r, kColExperience);
        e.treasure = cell_text(table, r, kColTreasure);
        e.quest = table.cell_int(r, kColQuest);
        e.flying = iequals(trim(table.cell(r, kColFly)), "Y");
        e.movement = cell_text(table, r, kColMovement);
        e.ai_type = cell_text(table, r, kColAiType);
        e.hostility = table.cell_int(r, kColHostility);
        e.speed = table.cell_int(r, kColSpeed);
        e.recovery = table.cell_int(r, kColRecovery);
        e.preference = cell_text(table, r, kColPreference);
        e.bonus = cell_text(table, r, kColBonus);
        for (std::size_t i = 0; i < e.attacks.size(); ++i) {
            const std::size_t base = kColAttacks + i * kAttackStride;
            MonsterAttack& a = e.attacks[i];
            a.type = cell_text(table, r, base);
            a.damage = cell_text(table, r, base + 1);
            a.missile = cell_text(table, r, base + 2);
            a.chance = table.cell_int(r, base + 3);
            a.damage_dice = parse_dice(a.damage);
            a.flies = !a.missile.empty() && a.missile != "0";
        }
        e.spell_percent = table.cell_int(r, kColSpellPercent);
        e.spells = cell_text(table, r, kColSpells);
        for (std::size_t i = 0; i < kResistanceCount; ++i) {
            e.resistances[i] = resistance_value(table.cell(r, kColResistances + i));
        }
        e.special = cell_text(table, r, kColSpecial);
        out.entries_.push_back(std::move(e));
    }
    return MonsterStatsError::None;
}

const MonsterStatsEntry* MonsterStatsTable::find(std::string_view picture) const noexcept {
    const std::string want = normalize_picture(picture);
    for (const auto& e : entries_) {
        if (normalize_picture(e.picture) == want)
            return &e;
    }
    return nullptr;
}

}  // namespace starhaven::data
