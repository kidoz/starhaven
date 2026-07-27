#include "core/data/merchant_text.hpp"

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

MerchantTextError MerchantTextTable::parse(const TextTable& table, MerchantTextTable& out) {
    for (auto& row : out.lines_) {
        for (auto& cell : row) {
            cell.clear();
        }
    }

    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, 1)), "Buy") && iequals(trim(table.cell(r, 2)), "Sell")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return MerchantTextError::NoHeader;
    }

    for (std::size_t s = 0; s < kMerchantSituationCount; ++s) {
        const std::size_t row = header + 1 + s;
        if (row >= table.row_count()) {
            break;
        }
        for (std::size_t a = 0; a < kMerchantActionCount; ++a) {
            std::string text(trim(table.cell(row, 1 + a)));
            // "n/a" is the table's way of saying this cannot come up.
            if (iequals(text, "n/a")) {
                continue;
            }
            out.lines_[s][a] = std::move(text);
        }
    }
    return MerchantTextError::None;
}

std::string_view MerchantTextTable::line(MerchantSituation situation,
                                         MerchantAction action) const noexcept {
    const auto s = static_cast<std::size_t>(situation);
    const auto a = static_cast<std::size_t>(action);
    if (s >= kMerchantSituationCount || a >= kMerchantActionCount) {
        return {};
    }
    return lines_[s][a];
}

std::size_t MerchantTextTable::filled() const noexcept {
    std::size_t count = 0;
    for (const auto& row : lines_) {
        for (const auto& cell : row) {
            count += cell.empty() ? 0 : 1;
        }
    }
    return count;
}

}  // namespace starhaven::data
