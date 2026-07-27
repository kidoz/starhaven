#ifndef STARHAVEN_CORE_DATA_PROFESSION_TEXT_HPP
#define STARHAVEN_CORE_DATA_PROFESSION_TEXT_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// The seven days `PROFTEXT.txt` has a column pair for, in its own order.
inline constexpr std::size_t kProfessionDayCount = 7;

// What a hired NPC of one profession talks about on one day.
struct ProfessionDay {
    std::string topic;
    std::string text;

    [[nodiscard]] bool empty() const noexcept { return topic.empty() && text.empty(); }
};

// One row of `PROFTEXT.txt`: a profession, and something to say on each day of
// the week.
struct ProfessionTextEntry {
    int id = 0;  // the same 1-based id `npcprof.txt` gives the profession
    std::string name;
    std::array<ProfessionDay, kProfessionDayCount> days;
};

enum class ProfessionTextError : std::uint8_t {
    None,
    // No row carries the expected day headings.
    NoHeader,
};

// `PROFTEXT.txt`, parsed into rows.
class ProfessionTextTable {
public:
    ProfessionTextTable() = default;

    [[nodiscard]] static ProfessionTextError parse(const TextTable& table,
                                                   ProfessionTextTable& out);

    [[nodiscard]] const std::vector<ProfessionTextEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Resolve a profession id. Returns nullptr when the table has no such row.
    [[nodiscard]] const ProfessionTextEntry* at(int id) const noexcept;

private:
    std::vector<ProfessionTextEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_PROFESSION_TEXT_HPP
