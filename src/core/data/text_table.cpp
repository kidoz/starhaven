#include "core/data/text_table.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::data {

namespace {

constexpr std::size_t kNameSize = 16;
constexpr std::size_t kUnpackedSizeOffset = 0x28;

// Windows-1252's 0x80..0x9F block, the only range where it differs from
// Latin-1. Zero marks the five unassigned slots.
constexpr std::array<char32_t, 32> kCp1252High{
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::string_view trim(std::string_view text) noexcept {
    auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

std::string cp1252_to_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        const auto b = static_cast<std::uint8_t>(raw);
        if (b < 0x80) {
            out.push_back(raw);
            continue;
        }
        char32_t cp = b;
        if (b < 0xA0) {
            cp = kCp1252High[b - 0x80];
            // An unassigned slot has no character to produce. U+FFFD says so
            // rather than inventing one.
            if (cp == 0)
                cp = 0xFFFD;
        }
        append_utf8(out, cp);
    }
    return out;
}

TextTableError TextTable::parse_body(std::string_view text, TextTable& out) {
    out.rows_.clear();

    std::vector<std::string> row;
    std::string field;
    std::size_t i = 0;
    bool row_started = false;

    auto end_field = [&] {
        row.push_back(std::move(field));
        field.clear();
    };
    auto end_row = [&] {
        end_field();
        out.rows_.push_back(std::move(row));
        row.clear();
        row_started = false;
    };

    while (i < text.size()) {
        // A field that opens with a quote is an Excel-style quoted field: it
        // may contain tabs and newlines, and `""` stands for one quote. The
        // shipped prose tables rely on both.
        if (field.empty() && text[i] == '"') {
            ++i;
            bool closed = false;
            while (i < text.size()) {
                if (text[i] == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        field.push_back('"');
                        i += 2;
                        continue;
                    }
                    ++i;
                    closed = true;
                    break;
                }
                field.push_back(text[i]);
                ++i;
            }
            if (!closed) {
                out.rows_.clear();
                return TextTableError::UnterminatedQuote;
            }
            row_started = true;
            continue;
        }

        const char c = text[i];
        if (c == '\t') {
            end_field();
            row_started = true;
            ++i;
        } else if (c == '\r' || c == '\n') {
            i += (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1;
            end_row();
        } else {
            field.push_back(c);
            row_started = true;
            ++i;
        }
    }

    // A table normally ends with a row terminator, which would otherwise leave
    // a spurious empty row behind. Anything actually pending is kept.
    if (row_started || !field.empty()) {
        end_row();
    }
    return TextTableError::None;
}

TextTableError TextTable::parse(std::span<const std::byte> entry, TextTable& out) {
    out.rows_.clear();
    out.name_.clear();
    if (entry.size() < kTextTableHeaderSize) {
        return TextTableError::TooSmall;
    }

    io::ByteReader r(entry);
    if (!r.read_fixed_string(kNameSize, out.name_)) {
        return TextTableError::TooSmall;
    }
    if (!r.seek(kUnpackedSizeOffset)) {
        return TextTableError::TooSmall;
    }
    const std::uint32_t unpacked = r.read_u32_le();
    if (!r.ok()) {
        return TextTableError::TooSmall;
    }

    const auto body = entry.subspan(kTextTableHeaderSize);

    // An entry declaring no unpacked size is stored as-is. One shipped table
    // (`errorlog.txt`, a leftover developer log) is like this, so treating the
    // case as an error would reject valid data.
    if (unpacked == 0) {
        return parse_body(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()),
                          out);
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(body, raw)) {
        return TextTableError::InflateFailed;
    }
    if (raw.size() != unpacked) {
        return TextTableError::SizeMismatch;
    }
    return parse_body(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), out);
}

std::string_view TextTable::cell(std::size_t row, std::size_t column) const noexcept {
    if (row >= rows_.size() || column >= rows_[row].size()) {
        return {};
    }
    return rows_[row][column];
}

int TextTable::cell_int(std::size_t row, std::size_t column, int fallback) const noexcept {
    return parse_int(cell(row, column), fallback);
}

int parse_int(std::string_view text, int fallback) noexcept {
    // Thousands separators are all over the shipped tables (`" 1,300 "`), so
    // strip them before reading. They are a spreadsheet artifact, not data.
    std::string digits;
    for (const char c : trim(text)) {
        if (c != ',')
            digits.push_back(c);
    }
    if (digits.empty()) {
        return fallback;
    }
    int value = 0;
    const char* first = digits.data();
    const char* last = first + digits.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    // The whole cell must be the number. "2-4" is a range and "5%6D20" is a
    // treasure code; reading either as 2 or 5 would be worse than refusing.
    if (ec != std::errc{} || ptr != last) {
        return fallback;
    }
    return value;
}

}  // namespace starhaven::data
