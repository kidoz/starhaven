#ifndef STARHAVEN_CORE_DATA_TEXT_TABLE_HPP
#define STARHAVEN_CORE_DATA_TEXT_TABLE_HPP

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::data {

enum class TextTableError {
    None,
    // Fewer bytes than the 48-byte container header needs.
    TooSmall,
    // The container's zlib stream is missing or malformed.
    InflateFailed,
    // The inflated length disagrees with the header's declared size.
    SizeMismatch,
    // A quoted field runs to the end of the buffer without closing.
    UnterminatedQuote,
};

// One of the game's tab-separated data tables, as shipped inside `icons.lod`.
//
// These are not a reverse-engineered format: they are spreadsheet exports the
// developers left in the archive, so the "decode" is a container unwrap plus a
// TSV parse. See docs/formats/text-tables.md.
class TextTable {
public:
    TextTable() = default;

    // Parse a raw `icons.lod` entry: the 48-byte container header, then either
    // a zlib stream or — when the header declares no unpacked size — the bytes
    // as stored.
    [[nodiscard]] static TextTableError parse(std::span<const std::byte> entry, TextTable& out);

    // Parse an already-unwrapped table body. Split out so tests can exercise
    // the TSV rules without building a container around every fixture.
    [[nodiscard]] static TextTableError parse_body(std::string_view text, TextTable& out);

    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] const std::vector<std::vector<std::string>>& rows() const noexcept {
        return rows_;
    }

    // A cell, or an empty string when either index is past the end. Rows are
    // ragged — the shipped tables have trailing columns present on some rows
    // and absent on others — so callers should not index blind.
    [[nodiscard]] std::string_view cell(std::size_t row, std::size_t column) const noexcept;

    // The cell parsed as an integer, or `fallback` when it is empty or is not
    // a number. Blank numeric cells are common in the shipped tables.
    // See `parse_int` for the exact rules.
    [[nodiscard]] int cell_int(std::size_t row, std::size_t column,
                               int fallback = 0) const noexcept;

    // The name the container header carries, e.g. "MAPSTATS.TXT". Independent
    // of the archive entry's own name and sometimes cased differently.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    std::vector<std::vector<std::string>> rows_;
};

// Convert Windows-1252 text to UTF-8. The tables are cp1252 — they carry curly
// quotes, en dashes and accented letters — so anything that reaches a terminal
// or a UTF-8 UI has to be converted rather than passed through.
[[nodiscard]] std::string cp1252_to_utf8(std::string_view text);

// Trim ASCII whitespace from both ends. The exports are full of padded cells
// (`" 2-4"`, `"  93  "`), and every caller wants them trimmed.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

// Read a cell as an integer. Leading and trailing space is ignored and
// thousands separators are stripped, because the tables are spreadsheet
// exports and carry both. Anything else — a range (`"2-4"`), a dice code
// (`"3D6+2"`), a treasure code (`"5%6D20+L2Bow"`) — yields `fallback`, since a
// partial read would silently turn `"2-4"` into 2.
[[nodiscard]] int parse_int(std::string_view text, int fallback = 0) noexcept;

// Container layout (see docs/formats/dtile.md, which documents the same shape).
constexpr std::size_t kTextTableHeaderSize = 48;

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_TEXT_TABLE_HPP
