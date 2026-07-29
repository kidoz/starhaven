#ifndef STARHAVEN_GAME_HIRE_HPP
#define STARHAVEN_GAME_HIRE_HPP

// Hired help, on the professions' own terms.
//
// `npcprof.txt` writes each profession's weekly cost and its benefit in the
// designers' prose — "Ten percent bonus on all experience learned", "All boat
// travel 2 days faster (minimum one day)", "Makes one day of food per day
// (maximum of 14 days)". This reads the numbers out of the phrasings the
// shipped 51 benefit rows actually use; a benefit whose mechanic this engine
// does not have yet (skill points, reputation, the flying spells) parses to
// nothing and is carried as prose only. The "%17 percent of all gold you
// find" tail names a share no column of the table carries; it stays
// `unknown` and untaken. See docs/formats/text-tables.md.

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "core/data/npc_stats.hpp"

namespace starhaven::game {

// What one hired profession does for the party, in numbers this engine can
// apply. Zero everywhere means the benefit is prose beyond this slice.
struct HireBenefit {
    int experience_percent = 0;  // bonus on experience gained
    int gold_percent = 0;        // bonus on gold found
    int coach_days_faster = 0;   // land routes, minimum one day
    int boat_days_faster = 0;    // sea routes, minimum one day
    int heal_level = 0;          // 1 hit points, 2 + conditions, 3 everything
    bool repairs_weapons = false;
    bool repairs_armor = false;
    bool identifies = false;  // the Scholar's "Unlimited item Identification"
    int luck_bonus = 0;            // to every character while hired
    int elemental_protection = 0;  // to the four elemental resistances
    int food_per_day = 0;          // a cook's make, capped by their own words
    int food_cap = 0;
    int food_saved_camping = 0;    // a porter's "less days of food use"
    int bless_hours = 0;    // cast at dawn, at the row's own duration
    int heroism_hours = 0;

    // The masters' skill bonuses, in the rows' own points: "Two point bonus
    // to all weapon skills", "Three point bonus to all spell skills", the
    // merchants' "Four point bonus to Merchant skill".
    int weapon_skill_bonus = 0;
    int spell_skill_bonus = 0;
    int merchant_skill_bonus = 0;
    int armor_skill_bonus = 0;  // the Squire's "Armor and weapon skills"

    [[nodiscard]] bool any() const noexcept {
        return experience_percent != 0 || gold_percent != 0 || coach_days_faster != 0 ||
               boat_days_faster != 0 || heal_level != 0 || repairs_weapons || repairs_armor ||
               identifies ||
               luck_bonus != 0 || elemental_protection != 0 || food_per_day != 0 ||
               food_saved_camping != 0 || bless_hours != 0 || heroism_hours != 0 ||
               weapon_skill_bonus != 0 || spell_skill_bonus != 0 ||
               merchant_skill_bonus != 0 || armor_skill_bonus != 0;
    }
};

namespace hire_detail {

[[nodiscard]] inline std::string lowered(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The number a word or digit run spells; the rows never pass twenty.
[[nodiscard]] inline int number_of(std::string_view word) {
    if (!word.empty() && std::isdigit(static_cast<unsigned char>(word.front())) != 0) {
        int value = 0;
        for (const char c : word) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                break;
            }
            value = value * 10 + (c - '0');
        }
        return value;
    }
    constexpr std::array<std::pair<std::string_view, int>, 14> kWords{
        {{"one", 1},
         {"two", 2},
         {"three", 3},
         {"four", 4},
         {"five", 5},
         {"six", 6},
         {"seven", 7},
         {"eight", 8},
         {"nine", 9},
         {"ten", 10},
         {"eleven", 11},
         {"twelve", 12},
         {"fifteen", 15},
         {"twenty", 20}}};
    for (const auto& [name, value] : kWords) {
        if (word.substr(0, name.size()) == name) {
            return value;
        }
    }
    return 0;
}

// The word immediately before `at` in `text`.
[[nodiscard]] inline std::string_view word_before(std::string_view text, std::size_t at) {
    while (at > 0 && text[at - 1] == ' ') {
        --at;
    }
    std::size_t start = at;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '(') {
        --start;
    }
    return text.substr(start, at - start);
}

// The number spelled just before a phrase, or 0.
[[nodiscard]] inline int number_before(const std::string& text, std::string_view phrase) {
    const std::size_t at = text.find(phrase);
    return at == std::string::npos ? 0 : number_of(word_before(text, at));
}

// The number spelled just after a phrase, or 0.
[[nodiscard]] inline int number_after(const std::string& text, std::string_view phrase) {
    const std::size_t at = text.find(phrase);
    if (at == std::string::npos) {
        return 0;
    }
    std::size_t p = at + phrase.size();
    while (p < text.size() && text[p] == ' ') {
        ++p;
    }
    return number_of(std::string_view(text).substr(p));
}

}  // namespace hire_detail

// Read one profession's benefit prose. Each phrasing below is one the
// shipped rows use verbatim; nothing is guessed past them.
[[nodiscard]] inline HireBenefit parse_benefit(std::string_view prose) {
    using namespace hire_detail;
    const std::string text = lowered(prose);
    HireBenefit out;

    // "5 percent bonus on all experience gained", "Ten percent ... learned".
    if (text.find("percent bonus on all experience") != std::string::npos) {
        out.experience_percent = number_before(text, "percent bonus on all experience");
    }
    // "Ten percent bonus on all gold found", "gold is increased by ten
    // percent when found".
    if (text.find("percent bonus on all gold") != std::string::npos) {
        out.gold_percent = number_before(text, "percent bonus on all gold");
    } else if (text.find("gold is increased by") != std::string::npos) {
        out.gold_percent = number_after(text, "gold is increased by");
    }

    // Land: "map crossings one day faster", "stables take two days fewer",
    // and "all travel times reduced by one day" reaches both kinds.
    if (text.find("map crossings") != std::string::npos) {
        out.coach_days_faster = number_after(text, "map crossings");
    } else if (text.find("stables take") != std::string::npos) {
        out.coach_days_faster = number_after(text, "stables take");
    }
    // Sea: "boat travel 2 days faster", "boat travel reduced by two days".
    if (text.find("boat travel reduced by") != std::string::npos) {
        out.boat_days_faster = number_after(text, "boat travel reduced by");
    } else if (text.find("boat travel") != std::string::npos) {
        out.boat_days_faster = number_after(text, "boat travel");
    }
    if (text.find("all travel times reduced by") != std::string::npos) {
        const int days = number_after(text, "all travel times reduced by");
        out.coach_days_faster = days;
        out.boat_days_faster = days;
    }

    // The healers' three rungs.
    if (text.find("completely heals") != std::string::npos) {
        out.heal_level = 3;
    } else if (text.find("cures all party hit points and conditions") != std::string::npos) {
        out.heal_level = 2;
    } else if (text.find("cures all party hit points") != std::string::npos) {
        out.heal_level = 1;
    }

    out.repairs_weapons = text.find("unlimited weapon repair") != std::string::npos;
    out.repairs_armor = text.find("unlimited armor repair") != std::string::npos;
    out.identifies = text.find("unlimited item identification") != std::string::npos;

    // "Five point bonus to Luck statistic", "Luck statistics are increased
    // by ten points".
    if (text.find("point bonus to luck statistic") != std::string::npos) {
        out.luck_bonus = number_before(text, "point bonus to luck statistic");
    } else if (text.find("luck statistics are increased by") != std::string::npos) {
        out.luck_bonus = number_after(text, "luck statistics are increased by");
    }

    // "Increases protection from the four elements by 20 constantly."
    if (text.find("protection from the four elements by") != std::string::npos) {
        out.elemental_protection = number_after(text, "protection from the four elements by");
    }

    // "Makes one day of food per day (maximum of 14 days)."
    if (text.find("of food per day") != std::string::npos) {
        out.food_per_day = number_after(text, "makes");
        out.food_cap = number_after(text, "maximum of");
    }

    // "One less day of food use when camping, (minimum of one used)", and
    // the Gypsy's "Food use is reduced by one day's worth of food when
    // resting (minimum one day)".
    if (text.find("less day") != std::string::npos &&
        text.find("food use") != std::string::npos) {
        out.food_saved_camping = number_before(text, "less day");
    } else if (text.find("food use is reduced by") != std::string::npos) {
        out.food_saved_camping = number_after(text, "food use is reduced by");
    }

    // "Two point bonus to all weapon skills for all characters", "Armor and
    // weapon skills are increased by two points for each character".
    if (text.find("point bonus to all weapon skills") != std::string::npos) {
        out.weapon_skill_bonus = number_before(text, "point bonus to all weapon skills");
    } else if (text.find("armor and weapon skills are increased by") != std::string::npos) {
        out.weapon_skill_bonus = number_after(text, "armor and weapon skills are increased by");
        out.armor_skill_bonus = out.weapon_skill_bonus;
    }
    if (text.find("point bonus to all spell skills") != std::string::npos) {
        out.spell_skill_bonus = number_before(text, "point bonus to all spell skills");
    }
    // "Four point bonus to Merchant skill", "Merchant skill is increased by
    // eight points".
    if (text.find("point bonus to merchant skill") != std::string::npos) {
        out.merchant_skill_bonus = number_before(text, "point bonus to merchant skill");
    } else if (text.find("merchant skill is increased by") != std::string::npos) {
        out.merchant_skill_bonus = number_after(text, "merchant skill is increased by");
    }

    // "Casts the Bless spell (duration 2 hours) at master ranking once per
    // day", and Heroism the same way.
    if (text.find("casts the bless spell") != std::string::npos) {
        out.bless_hours = number_after(text, "duration");
    }
    if (text.find("casts the heroism spell") != std::string::npos) {
        out.heroism_hours = number_after(text, "duration");
    }
    return out;
}

// One hired person: enough to draw them, pay them, and find their row again.
struct Hireling {
    int npc_id = 0;
    std::string name;
    int profession_id = 0;
    std::string profession;
    int weekly_cost = 0;
    HireBenefit benefit;
};

// How many the party may keep. The original's follower panel seats two;
// the number here is this engine's reading of that panel.
inline constexpr std::size_t kHirelingLimit = 2;

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_HIRE_HPP
