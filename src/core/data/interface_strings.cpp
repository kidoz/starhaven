#include "core/data/interface_strings.hpp"

#include <cstddef>

namespace starhaven::data {

InterfaceStringsError InterfaceStrings::parse(const TextTable& table, InterfaceStrings& out) {
    out.strings_.clear();

    bool any = false;
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        // Ids start at zero, so the fallback has to be negative rather than
        // zero: the first string would otherwise be dropped.
        const int id = table.cell_int(r, 0, -1);
        if (id < 0) {
            continue;
        }
        std::string text(trim(table.cell(r, 1)));
        if (text.empty()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(id);
        if (index >= out.strings_.size()) {
            out.strings_.resize(index + 1);
        }
        out.strings_[index] = std::move(text);
        any = true;
    }
    return any ? InterfaceStringsError::None : InterfaceStringsError::NoRows;
}

std::string_view InterfaceStrings::at(int id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= strings_.size()) {
        return {};
    }
    return strings_[static_cast<std::size_t>(id)];
}

}  // namespace starhaven::data
