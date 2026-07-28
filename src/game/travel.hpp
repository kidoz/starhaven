#ifndef STARHAVEN_GAME_TRAVEL_HPP
#define STARHAVEN_GAME_TRAVEL_HPP

// Riding the coach and sailing the boat.
//
// A stables' or docks' row in `2DEvents.txt` writes its routes in the three
// stock columns, the designers' way: `"Castle Ironfist D3,M,W,F,2"` is a
// destination with its area code, the weekdays a ride leaves, and the days
// the ride takes. The sheet's own margin notes name the parts — "Destination,
// Area", "Days leaving,", "Days Travel Time", "Upto 3 Destinations",
// "Teleport to Other Stable / Dock or Land". `observed` What the shipped
// data does not carry is where you stand on arrival and what the fare is in
// gold, so both are this engine's: arrival is the destination map's own
// starting point, and the fare scales the row's `Val` column.

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/building_stats.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/text_table.hpp"
#include "game/clock.hpp"

namespace starhaven::game {

// What a point of `Val` costs in gold. No table says. `inferred`
inline constexpr int kFarePerVal = 25;

// One route a travel establishment offers.
struct TravelRoute {
    std::string destination;      // the cell's own words, e.g. "Castle Ironfist"
    std::string map_file;         // "OutD3.Odm"; empty when unresolved
    std::array<bool, 7> leaves{};  // by kWeekdays index, Sunday first
    int days = 0;                 // how long the ride takes

    [[nodiscard]] bool empty() const noexcept { return map_file.empty(); }
    [[nodiscard]] bool leaves_on(std::int64_t day) const noexcept {
        return leaves[static_cast<std::size_t>(day % 7)];
    }
};

// A route cell's weekday tokens: `M,Tu,W,Th,F,Sa,Su` — and the sheet also
// writes `T` for Tuesday and `Sun` for Sunday. Returns the kWeekdays index,
// or -1 for anything else.
[[nodiscard]] inline int weekday_index(std::string_view token) noexcept {
    const auto is = [token](std::string_view word) {
        if (token.size() != word.size()) {
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(token[i])) !=
                std::tolower(static_cast<unsigned char>(word[i]))) {
                return false;
            }
        }
        return true;
    };
    if (is("Su") || is("Sun")) {
        return 0;
    }
    if (is("M")) {
        return 1;
    }
    if (is("T") || is("Tu")) {
        return 2;
    }
    if (is("W")) {
        return 3;
    }
    if (is("Th")) {
        return 4;
    }
    if (is("F")) {
        return 5;
    }
    if (is("Sa")) {
        return 6;
    }
    return -1;
}

// Read one route cell. The destination is everything before the first comma;
// its trailing area code — a letter A..E and a digit — names the outdoor map,
// and a destination without one is matched against the design table's own
// display names. The rest is weekdays and, last, the days of travel.
[[nodiscard]] inline TravelRoute parse_route(std::string_view cell,
                                             const data::MapStatsTable& maps) {
    TravelRoute out;
    const std::string_view text = data::trim(cell);
    if (text.empty() || text == "-") {
        return out;
    }

    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ',') {
            parts.push_back(data::trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (parts.empty() || parts.front().empty()) {
        return out;
    }

    // The destination, and its area code if the last word is one.
    std::string_view destination = parts.front();
    std::string code;
    if (const std::size_t space = destination.rfind(' '); space != std::string_view::npos) {
        const std::string_view last = destination.substr(space + 1);
        if (last.size() == 2 && last[0] >= 'A' && last[0] <= 'E' &&
            std::isdigit(static_cast<unsigned char>(last[1])) != 0) {
            code = std::string(last);
            destination = data::trim(destination.substr(0, space));
        }
    }
    out.destination = std::string(destination);
    if (!code.empty()) {
        out.map_file = "Out" + code + ".Odm";
    } else {
        // "Bootleg Bay East" writes no code; the display name is the join.
        for (const auto& m : maps.entries()) {
            const std::string_view name = m.name;
            if (name.size() < destination.size()) {
                continue;
            }
            bool same = true;
            for (std::size_t i = 0; i < destination.size() && same; ++i) {
                same = std::tolower(static_cast<unsigned char>(name[i])) ==
                       std::tolower(static_cast<unsigned char>(destination[i]));
            }
            if (same) {
                out.map_file = m.file_name;
                break;
            }
        }
        if (out.map_file.empty()) {
            return out;  // "Any outdoor area" and the like stay unresolved
        }
    }

    for (std::size_t i = 1; i < parts.size(); ++i) {
        if (const int day = weekday_index(parts[i]); day >= 0) {
            out.leaves[static_cast<std::size_t>(day)] = true;
        } else {
            out.days = data::parse_int(parts[i], out.days);
        }
    }
    return out;
}

// Whether an establishment sells rides rather than things.
[[nodiscard]] inline bool is_travel(const data::BuildingStatsEntry& shop) noexcept {
    return shop.type == "Stables" || shop.type == "Boats";
}

// The routes a travel row offers: its three stock cells, read as routes.
[[nodiscard]] inline std::vector<TravelRoute> routes_of(const data::BuildingStatsEntry& shop,
                                                        const data::MapStatsTable& maps) {
    std::vector<TravelRoute> out;
    for (const std::string_view cell :
         {std::string_view(shop.stock_a), std::string_view(shop.stock_b),
          std::string_view(shop.stock_c)}) {
        if (const TravelRoute route = parse_route(cell, maps); !route.empty()) {
            out.push_back(route);
        }
    }
    return out;
}

// What the ride costs: the row's own `Val`, scaled. `inferred`
[[nodiscard]] inline int fare_of(const data::BuildingStatsEntry& shop) noexcept {
    const auto fare = static_cast<int>(shop.price_factor * static_cast<float>(kFarePerVal));
    return fare < 1 ? 1 : fare;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TRAVEL_HPP
