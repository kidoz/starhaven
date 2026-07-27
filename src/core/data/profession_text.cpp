#include "core/data/profession_text.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

constexpr std::size_t kColId = 0;
constexpr std::size_t kColName = 1;
constexpr std::size_t kColFirstDay = 2;

bool icontains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool same = true;
        for (std::size_t j = 0; j < needle.size() && same; ++j) {
            same = std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                   std::tolower(static_cast<unsigned char>(needle[j]));
        }
        if (same) {
            return true;
        }
    }
    return false;
}

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

}  // namespace

ProfessionTextError ProfessionTextTable::parse(const TextTable& table, ProfessionTextTable& out) {
    out.entries_.clear();

    // The heading row names the days in its first day column.
    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (icontains(table.cell(r, kColFirstDay), "Sunday")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return ProfessionTextError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const int id = table.cell_int(r, kColId, -1);
        std::string name = cell_text(table, r, kColName);
        if (id <= 0 || name.empty()) {
            continue;
        }
        ProfessionTextEntry entry;
        entry.id = id;
        entry.name = std::move(name);
        for (std::size_t day = 0; day < kProfessionDayCount; ++day) {
            entry.days[day].topic = cell_text(table, r, kColFirstDay + day * 2);
            entry.days[day].text = cell_text(table, r, kColFirstDay + day * 2 + 1);
        }
        out.entries_.push_back(std::move(entry));
    }
    return ProfessionTextError::None;
}

const ProfessionTextEntry* ProfessionTextTable::at(int id) const noexcept {
    for (const auto& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data
