#ifndef STARHAVEN_GAME_PROMOTION_HPP
#define STARHAVEN_GAME_PROMOTION_HPP

// Promotions, joined from two tables that already speak of each other.
//
// `Awards.txt` rows 8..31 read "Received Promotion to Crusader" — every
// promotion and its Honorary twin — and quest events set them through the
// award variable. `Class.txt` lists the eighteen classes in six ladders of
// three, and each promoted class's own prose states its worth: "Cavaliers
// enjoy the benefit of an extra two hit points per level", "High Priests
// enjoy the benefit of an extra two hit points and spell points per level".
// This joins them: a plain promotion award steps the matching ladder and
// pays the prose's own per-level gains, retroactively for the levels held
// and on every level after. An Honorary award names no class change and is
// worn as an honor only — the original's own distinction, read from the two
// award rows existing side by side. `observed` for the phrasings and the
// ladder order; `inferred` for Honorary staying a title.

#include <cctype>
#include <string>
#include <string_view>

#include "core/data/spell_stats.hpp"
#include "game/party.hpp"

namespace starhaven::game {

// What a promoted class adds per level over its base, from its own words.
struct ClassGains {
    int hp_per_level = 0;
    int sp_per_level = 0;
};

namespace promotion_detail {

[[nodiscard]] inline std::string lowered(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Case- and space-insensitive: the award says "Archmage", the class table
// "Arch Mage".
[[nodiscard]] inline std::string squeezed(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c != ' ') {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

[[nodiscard]] inline int word_number(std::string_view word) {
    if (word.substr(0, 3) == "two") {
        return 2;
    }
    if (word.substr(0, 5) == "three") {
        return 3;
    }
    if (word.substr(0, 4) == "four") {
        return 4;
    }
    // "an extra hit point" spells no number: one.
    return 1;
}

}  // namespace promotion_detail

// Read a class's per-level bonus from its "enjoy the benefit of an extra
// ..." sentence. A base class has no such sentence and reads as zero.
[[nodiscard]] inline ClassGains parse_class_gains(std::string_view prose) {
    using namespace promotion_detail;
    const std::string text = lowered(prose);
    ClassGains out;
    const std::size_t at = text.find("benefit of an extra ");
    if (at == std::string::npos) {
        return out;
    }
    const std::string_view after = std::string_view(text).substr(at + 20);
    const std::size_t per_level = after.find("per level");
    if (per_level == std::string_view::npos) {
        return out;
    }
    const std::string_view phrase = after.substr(0, per_level);
    if (phrase.find("hit point") == std::string_view::npos) {
        return out;
    }
    out.hp_per_level = word_number(phrase);
    // "two hit points and spell points", "hit point and (extra) spell
    // point": the one number covers both when spell points are in the
    // phrase.
    if (phrase.find("spell point") != std::string_view::npos) {
        out.sp_per_level = out.hp_per_level;
    }
    return out;
}

// The class a promotion award names, or empty: "Received Promotion to
// Crusader" gives "Crusader"; an Honorary award gives nothing to change.
[[nodiscard]] inline std::string promotion_of(std::string_view award_text) {
    constexpr std::string_view kLead = "Received Promotion to ";
    if (award_text.substr(0, kLead.size()) != kLead) {
        return {};
    }
    std::string_view name = award_text.substr(kLead.size());
    if (name.substr(0, 9) == "Honorary ") {
        return {};
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) {
        name.remove_suffix(1);
    }
    return std::string(name);
}

// Whether `target` is a later step of `current`'s own ladder, and where.
// The eighteen classes sit in `Class.txt` in six ladders of three, in row
// order; both names are matched ignoring case and spaces.
[[nodiscard]] inline bool promotes_to(const data::DescriptionTable& classes,
                                      std::string_view current, std::string_view target) {
    using promotion_detail::squeezed;
    const auto& rows = classes.entries();
    std::size_t current_at = rows.size();
    std::size_t target_at = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (squeezed(rows[i].name) == squeezed(current)) {
            current_at = i;
        }
        if (squeezed(rows[i].name) == squeezed(target)) {
            target_at = i;
        }
    }
    return current_at < rows.size() && target_at < rows.size() &&
           current_at / 3 == target_at / 3 && target_at > current_at;
}

// Step a character up, paying the difference between the target's stated
// per-level gains and the ones their current class already paid — the
// "extra" each row states is over the ladder's base — for the levels held.
inline void promote(Character& who, const data::DescriptionTable& classes,
                    std::string_view target) {
    using promotion_detail::squeezed;
    const auto gains_of = [&classes](std::string_view name) {
        for (const auto& row : classes.entries()) {
            if (squeezed(row.name) == squeezed(name)) {
                return parse_class_gains(row.text.empty() ? "" : row.text.front());
            }
        }
        return ClassGains{};
    };
    const ClassGains before = gains_of(who.class_name);
    const ClassGains after = gains_of(target);
    const int hp = (after.hp_per_level - before.hp_per_level) * who.level;
    const int sp = (after.sp_per_level - before.sp_per_level) * who.level;
    who.max_hit_points += hp > 0 ? hp : 0;
    who.hit_points += hp > 0 ? hp : 0;
    if (who.max_spell_points > 0 || sp > 0) {
        who.max_spell_points += sp > 0 ? sp : 0;
        who.spell_points += sp > 0 ? sp : 0;
    }
    // The table's own spelling of the class, so the sheet and the ladder
    // agree thereafter.
    for (const auto& row : classes.entries()) {
        if (squeezed(row.name) == squeezed(target)) {
            who.class_name = row.name;
            break;
        }
    }
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_PROMOTION_HPP
