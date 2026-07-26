#ifndef STARHAVEN_CORE_DATA_INTERFACE_STRINGS_HPP
#define STARHAVEN_CORE_DATA_INTERFACE_STRINGS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

enum class InterfaceStringsError : std::uint8_t {
    None,
    // No row carries a number, so the table is not `Global.txt`.
    NoRows,
};

// `Global.txt`: the interface's own vocabulary, by id. "AC", "Accuracy",
// "Add to Stat", "Adventurer" — the words the original drew on its panels.
class InterfaceStrings {
public:
    InterfaceStrings() = default;

    [[nodiscard]] static InterfaceStringsError parse(const TextTable& table, InterfaceStrings& out);

    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

    // The string with this id, or empty when there is none. Ids start at zero
    // and the shipped table has no gaps, but a missing one is not an error.
    [[nodiscard]] std::string_view at(int id) const noexcept;

private:
    std::vector<std::string> strings_;  // indexed by id
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_INTERFACE_STRINGS_HPP
