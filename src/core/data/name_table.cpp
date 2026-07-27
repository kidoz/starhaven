#include "core/data/name_table.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

NameTableError NameTable::parse(const TextTable& table, NameTable& out) {
    out.male_.clear();
    out.female_.clear();

    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, 0)), "Male") && iequals(trim(table.cell(r, 1)), "Female")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return NameTableError::NoHeader;
    }

    // The two columns are not the same length: the file runs out of one before
    // the other, and a blank cell is the end of that column, not of the row.
    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        if (std::string one(trim(table.cell(r, 0))); !one.empty()) {
            out.male_.push_back(std::move(one));
        }
        if (std::string two(trim(table.cell(r, 1))); !two.empty()) {
            out.female_.push_back(std::move(two));
        }
    }
    return NameTableError::None;
}

std::string_view NameTable::name(bool female, std::size_t n) const noexcept {
    const std::vector<std::string>& column = female ? female_ : male_;
    if (column.empty()) {
        return {};
    }
    return column[n % column.size()];
}

}  // namespace starhaven::data
