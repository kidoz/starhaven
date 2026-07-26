#ifndef STARHAVEN_CORE_DATA_JOURNAL_HPP
#define STARHAVEN_CORE_DATA_JOURNAL_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// One line of the player's journal: a quest note, an award, or an automatic
// note. All three tables are keyed the same way — the game sets bit N when
// something happens and the journal shows line N.
struct JournalEntry {
    int bit = 0;
    std::string text;       // what the player reads; empty where none was written
    std::string category;   // autonotes only: "Stat", "Teacher", ...
    std::string notes;      // the designers' own, not shown to a player
    std::string alternate;  // quests only: an older wording of the note

    [[nodiscard]] bool has_text() const noexcept { return !text.empty(); }
};

enum class JournalError : std::uint8_t {
    None,
    // No row carries a bit number, so the table is not one of these.
    NoRows,
};

// A bit-keyed table of journal lines. The column layout differs between the
// three files, so the caller says which column is which; `kNoColumn` marks one
// the file does not have.
class JournalTable {
public:
    static constexpr std::size_t kNoColumn = static_cast<std::size_t>(-1);

    JournalTable() = default;

    [[nodiscard]] static JournalError parse(const TextTable& table, std::size_t text_column,
                                            std::size_t category_column, std::size_t notes_column,
                                            std::size_t alternate_column, JournalTable& out);

    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // How many entries carry text a player would see.
    [[nodiscard]] std::size_t written() const noexcept;

    // Resolve a bit. Returns nullptr when the table has no such line.
    [[nodiscard]] const JournalEntry* at(int bit) const noexcept;

private:
    std::vector<JournalEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_JOURNAL_HPP
