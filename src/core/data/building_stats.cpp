#include "core/data/building_stats.hpp"

#include <cctype>
#include <cstddef>

namespace starhaven::data {

namespace {

constexpr std::size_t kColId = 0;
constexpr std::size_t kColTypeId = 1;
constexpr std::size_t kColType = 2;
constexpr std::size_t kColMap = 3;
constexpr std::size_t kColName = 5;
constexpr std::size_t kColProprietor = 6;
constexpr std::size_t kColTitle = 7;
constexpr std::size_t kColStockA = 13;
constexpr std::size_t kColStockB = 14;
constexpr std::size_t kColStockC = 15;
constexpr std::size_t kColNotes = 16;
constexpr std::size_t kColOpens = 18;
constexpr std::size_t kColCloses = 19;

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

std::string cell_text(const TextTable& t, std::size_t row, std::size_t col) {
    return std::string(trim(t.cell(row, col)));
}

// A map code is a letter and a digit: A1 through E3, the fifteen outdoor maps.
bool is_map_code(std::string_view text) noexcept {
    return text.size() == 2 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 &&
           std::isdigit(static_cast<unsigned char>(text[1])) != 0;
}

}  // namespace

std::string_view BuildingStatsEntry::map_code() const noexcept {
    return is_map_code(map) ? std::string_view(map) : std::string_view{};
}

BuildingStatsError BuildingStatsTable::parse(const TextTable& table, BuildingStatsTable& out) {
    out.entries_.clear();

    std::size_t header = table.row_count();
    for (std::size_t r = 0; r < table.row_count(); ++r) {
        if (iequals(trim(table.cell(r, kColType)), "Type") &&
            iequals(trim(table.cell(r, kColMap)), "Map")) {
            header = r;
            break;
        }
    }
    if (header == table.row_count()) {
        return BuildingStatsError::NoHeader;
    }

    for (std::size_t r = header + 1; r < table.row_count(); ++r) {
        const int id = table.cell_int(r, kColId, -1);
        std::string type = cell_text(table, r, kColType);
        if (id <= 0 || type.empty()) {
            continue;
        }

        BuildingStatsEntry e;
        e.id = id;
        e.type_id = table.cell_int(r, kColTypeId);
        e.type = std::move(type);
        e.map = cell_text(table, r, kColMap);
        e.name = cell_text(table, r, kColName);
        e.proprietor = cell_text(table, r, kColProprietor);
        e.title = cell_text(table, r, kColTitle);
        e.stock_a = cell_text(table, r, kColStockA);
        e.stock_b = cell_text(table, r, kColStockB);
        e.stock_c = cell_text(table, r, kColStockC);
        e.notes = cell_text(table, r, kColNotes);
        e.opens = table.cell_int(r, kColOpens);
        e.closes = table.cell_int(r, kColCloses);
        out.entries_.push_back(std::move(e));
    }
    return BuildingStatsError::None;
}

std::vector<const BuildingStatsEntry*> BuildingStatsTable::on_map(std::string_view code) const {
    std::vector<const BuildingStatsEntry*> out;
    if (code.empty()) {
        return out;
    }
    for (const auto& e : entries_) {
        if (iequals(e.map_code(), code)) {
            out.push_back(&e);
        }
    }
    return out;
}

std::string map_code_of(std::string_view file_name) {
    // "OutE3.Odm" -> "E3". Anything else, including every indoor map, has no
    // code: the table only ever names outdoor ones.
    constexpr std::string_view prefix = "out";
    if (file_name.size() < prefix.size() + 2 ||
        !iequals(file_name.substr(0, prefix.size()), prefix)) {
        return {};
    }
    const std::string_view code = file_name.substr(prefix.size(), 2);
    if (!is_map_code(code)) {
        return {};
    }
    // The archive spells map names inconsistently — "OutA1.Odm", "outa1.odm" —
    // so the code is normalised to the table's own upper case.
    return std::string{static_cast<char>(std::toupper(static_cast<unsigned char>(code[0]))),
                       code[1]};
}

}  // namespace starhaven::data
