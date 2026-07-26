#include "core/data/journal.hpp"

#include <cstddef>

namespace starhaven::data {

namespace {

constexpr std::size_t kBitColumn = 0;

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    if (col == JournalTable::kNoColumn) {
        return {};
    }
    return std::string(trim(t.cell(row, col)));
}

}  // namespace

JournalError JournalTable::parse(const TextTable& table, std::size_t text_column,
                                 std::size_t category_column, std::size_t notes_column,
                                 std::size_t alternate_column, JournalTable& out) {
    out.entries_.clear();

    for (std::size_t r = 0; r < table.row_count(); ++r) {
        const int bit = table.cell_int(r, kBitColumn, -1);
        if (bit <= 0) {
            continue;
        }
        JournalEntry e;
        e.bit = bit;
        e.text = cell_text(table, r, text_column);
        e.category = cell_text(table, r, category_column);
        e.notes = cell_text(table, r, notes_column);
        e.alternate = cell_text(table, r, alternate_column);
        // A bit with no text is kept: the numbering is what the game sets, and
        // a gap in it would shift every line after.
        out.entries_.push_back(std::move(e));
    }
    if (out.entries_.empty()) {
        return JournalError::NoRows;
    }
    return JournalError::None;
}

std::size_t JournalTable::written() const noexcept {
    std::size_t count = 0;
    for (const auto& e : entries_) {
        count += e.has_text() ? 1 : 0;
    }
    return count;
}

const JournalEntry* JournalTable::at(int bit) const noexcept {
    for (const auto& e : entries_) {
        if (e.bit == bit) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data
