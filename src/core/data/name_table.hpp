#ifndef STARHAVEN_CORE_DATA_NAME_TABLE_HPP
#define STARHAVEN_CORE_DATA_NAME_TABLE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

enum class NameTableError : std::uint8_t {
    None,
    // No row carries the expected "Male" / "Female" header.
    NoHeader,
};

// `npcnames.txt`: two columns of given names, one per sex, which the game
// draws on when it needs to name somebody.
class NameTable {
public:
    NameTable() = default;

    [[nodiscard]] static NameTableError parse(const TextTable& table, NameTable& out);

    [[nodiscard]] const std::vector<std::string>& male() const noexcept { return male_; }
    [[nodiscard]] const std::vector<std::string>& female() const noexcept { return female_; }
    [[nodiscard]] std::size_t size() const noexcept { return male_.size() + female_.size(); }

    // The nth name of either column, wrapping. Empty when the column is.
    [[nodiscard]] std::string_view name(bool female, std::size_t n) const noexcept;

private:
    std::vector<std::string> male_;
    std::vector<std::string> female_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_NAME_TABLE_HPP
