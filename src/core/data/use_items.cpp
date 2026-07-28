#include "core/data/use_items.hpp"

#include <cctype>

namespace starhaven::data {

namespace {

// "Cure 10 Hit points" / "Cure 2 Spell points": the one effect family this
// engine applies directly. Everything else stays in the prose.
void parse_cures(UseItemEntry& entry) {
    const std::string_view text = entry.effect;
    if (text.substr(0, 5) != "Cure ") {
        return;
    }
    std::size_t at = 5;
    int amount = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
        amount = amount * 10 + (text[at] - '0');
        ++at;
    }
    if (amount == 0) {
        return;
    }
    const std::string_view rest = text.substr(at);
    if (rest.find("Hit point") != std::string_view::npos) {
        entry.cure_hit_points = amount;
    } else if (rest.find("Spell point") != std::string_view::npos) {
        entry.cure_spell_points = amount;
    }
}

MixResult parse_mix(std::string_view cell) {
    MixResult out;
    if (cell.empty() || cell == "no" || cell == "-") {
        return out;
    }
    if ((cell[0] == 'E' || cell[0] == 'e') && cell.size() == 2 && cell[1] >= '1' &&
        cell[1] <= '4') {
        out.kind = MixKind::Explosion;
        out.explosion_grade = cell[1] - '0';
        return out;
    }
    const int id = parse_int(cell, 0);
    if (id > 0) {
        out.kind = MixKind::Item;
        out.item_id = id;
    }
    return out;
}

}  // namespace

UseItemError UseItemTable::parse(const TextTable& table, UseItemTable& out) {
    out.entries_.clear();
    out.mix_ids_.clear();

    // The id header row lists the matrix's columns: a run of numeric cells
    // starting from the first item id.
    constexpr std::size_t kWidestRow = 64;
    std::size_t header = table.row_count();
    std::size_t first_column = 0;
    for (std::size_t row = 0; row < table.row_count() && header == table.row_count(); ++row) {
        for (std::size_t column = 1; column + 1 < kWidestRow; ++column) {
            if (parse_int(table.cell(row, column), 0) >= 100 &&
                parse_int(table.cell(row, column + 1), 0) ==
                    parse_int(table.cell(row, column), 0) + 1) {
                header = row;
                first_column = column;
                break;
            }
        }
    }
    if (header == table.row_count()) {
        return UseItemError::NoHeader;
    }
    for (std::size_t column = first_column; column < kWidestRow; ++column) {
        const int id = parse_int(table.cell(header, column), 0);
        if (id <= 0) {
            break;
        }
        out.mix_ids_.push_back(id);
    }

    for (std::size_t row = header + 1; row < table.row_count(); ++row) {
        UseItemEntry entry;
        entry.id = parse_int(table.cell(row, 0), 0);
        if (entry.id <= 0) {
            continue;
        }
        entry.name = std::string(trim(table.cell(row, 2)));
        entry.kind = std::string(trim(table.cell(row, 3)));
        entry.effect = std::string(trim(table.cell(row, 4)));
        parse_cures(entry);

        const std::string_view after = trim(table.cell(row, 5));
        entry.removed_when_used = after == "remove Item";
        if (const std::size_t to = after.rfind(' ');
            after.substr(0, 7) == "Change " && to != std::string_view::npos) {
            entry.becomes_item = parse_int(after.substr(to + 1), 0);
        }

        for (std::size_t i = 0; i < out.mix_ids_.size(); ++i) {
            entry.mixes.push_back(parse_mix(trim(table.cell(row, first_column + i))));
        }
        out.entries_.push_back(std::move(entry));
    }
    return UseItemError::None;
}

const UseItemEntry* UseItemTable::find(int item_id) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.id == item_id) {
            return &entry;
        }
    }
    return nullptr;
}

MixResult UseItemTable::mix(int first_item, int second_item) const noexcept {
    const UseItemEntry* row = find(first_item);
    if (row == nullptr) {
        return {};
    }
    for (std::size_t i = 0; i < mix_ids_.size() && i < row->mixes.size(); ++i) {
        if (mix_ids_[i] == second_item) {
            return row->mixes[i];
        }
    }
    return {};
}

int scroll_spell_of(std::string_view modifier) noexcept {
    const std::string_view text = trim(modifier);
    if (text.size() < 2 || (text[0] != 'S' && text[0] != 's')) {
        return 0;
    }
    int spell = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
            return 0;
        }
        spell = spell * 10 + (text[i] - '0');
    }
    return spell;
}

}  // namespace starhaven::data
