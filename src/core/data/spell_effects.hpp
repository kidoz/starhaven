#ifndef STARHAVEN_CORE_DATA_SPELL_EFFECTS_HPP
#define STARHAVEN_CORE_DATA_SPELL_EFFECTS_HPP

// The numbers inside a spell's prose.
//
// `Spells.txt` states what a spell does in the designers' words, and the
// damage and healing among them follow a few phrasings: `"does 2-6 points
// of damage"`, `"does 8 points of damage plus 1-2 per point of skill"`,
// `"Damage is 1-4 points of damage per point of skill"`, `"Cures 5 hit
// points"`, `"heals a single character of 3-7 hit points"`. This reads
// those numbers out without inventing any; a spell whose prose carries none
// parses to an empty effect and stays beyond this slice.

#include <cctype>
#include <cstdint>
#include <string_view>

#include "core/data/spell_stats.hpp"

namespace starhaven::data {

// An amount written as `N` or `N-M`.
struct SpellRange {
    int low = 0;
    int high = 0;

    [[nodiscard]] bool empty() const noexcept { return low <= 0 || high < low; }
};

// What one spell does, as far as its prose states it: a flat part, a part
// per point of skill, or a healing amount.
// How wide a damaging spell reaches, by its own prose.
enum class SpellReach : std::uint8_t {
    Single,  // "targets a single monster"
    Blast,   // "explodes to hurt anyone else caught in the blast"
    Sight,   // "all monsters in sight", "all creatures in sight"
};

struct SpellEffect {
    SpellRange heal;
    SpellRange damage;            // the flat part
    SpellRange damage_per_skill;  // the part that scales
    SpellReach reach = SpellReach::Single;

    [[nodiscard]] bool empty() const noexcept {
        return heal.empty() && damage.empty() && damage_per_skill.empty();
    }
};

namespace detail {

[[nodiscard]] inline bool same_ignoring_case(std::string_view a, std::string_view b) {
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

[[nodiscard]] inline std::size_t find_ignoring_case(std::string_view text, std::string_view word,
                                                    std::size_t from = 0) {
    if (word.empty() || text.size() < word.size()) {
        return std::string_view::npos;
    }
    for (std::size_t at = from; at + word.size() <= text.size(); ++at) {
        if (same_ignoring_case(text.substr(at, word.size()), word)) {
            return at;
        }
    }
    return std::string_view::npos;
}

// Read `N` or `N-M` starting at `at`; `end` reports one past the last digit.
[[nodiscard]] inline SpellRange range_at(std::string_view text, std::size_t at,
                                         std::size_t* end = nullptr) {
    SpellRange out;
    std::size_t p = at;
    while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) != 0) {
        out.low = out.low * 10 + (text[p] - '0');
        ++p;
    }
    if (out.low > 0) {
        out.high = out.low;
        if (p < text.size() && text[p] == '-') {
            int high = 0;
            std::size_t q = p + 1;
            while (q < text.size() && std::isdigit(static_cast<unsigned char>(text[q])) != 0) {
                high = high * 10 + (text[q] - '0');
                ++q;
            }
            if (high >= out.low) {
                out.high = high;
                p = q;
            }
        }
    }
    if (end != nullptr) {
        *end = p;
    }
    return out;
}

// The range right after a phrase, skipping spaces: `"does " -> 2-6`.
[[nodiscard]] inline SpellRange range_after(std::string_view text, std::string_view phrase,
                                            std::size_t* end = nullptr) {
    std::size_t from = 0;
    while (true) {
        const std::size_t at = find_ignoring_case(text, phrase, from);
        if (at == std::string_view::npos) {
            return {};
        }
        std::size_t p = at + phrase.size();
        while (p < text.size() && text[p] == ' ') {
            ++p;
        }
        if (const SpellRange range = range_at(text, p, end); !range.empty()) {
            return range;
        }
        from = at + 1;
    }
}

}  // namespace detail

// Read the numbers a spell's prose states at one mastery (0 normal,
// 1 expert, 2 master). The mastery cells carry the cures that differ by
// rank; the description carries the damage.
[[nodiscard]] inline SpellEffect parse_spell_effect(const SpellStatsEntry& spell, int mastery) {
    using detail::find_ignoring_case;
    using detail::range_after;

    SpellEffect out;
    const std::string_view rank = mastery >= 2   ? spell.master
                                  : mastery == 1 ? spell.expert
                                                 : spell.normal;
    // "Cures 7 hit points" in the rank cell wins; the description's amount
    // is the normal rank's.
    out.heal = range_after(rank, "cures ");
    const std::string_view text = spell.description;
    if (out.heal.empty()) {
        for (const std::string_view phrase : {"cures ", "character of ", "heals "}) {
            out.heal = range_after(text, phrase);
            if (!out.heal.empty() && find_ignoring_case(text, "hit point") !=
                                         std::string_view::npos) {
                break;
            }
            out.heal = {};
        }
    }

    // How far it reaches, in the designers' own words: "all monsters in
    // sight" and "all creatures in sight" name the whole room; "explodes",
    // "blast", "large radius" and "damage all creatures nearby" name a
    // burst around what was aimed at; everything else is one target.
    // `observed` for the phrases, `inferred` for the two-way split.
    if (find_ignoring_case(text, "in sight") != std::string_view::npos ||
        find_ignoring_case(text, "in the casters sight") != std::string_view::npos) {
        out.reach = SpellReach::Sight;
    } else if (find_ignoring_case(text, "explode") != std::string_view::npos ||
               find_ignoring_case(text, "blast") != std::string_view::npos ||
               find_ignoring_case(text, "large radius") != std::string_view::npos ||
               find_ignoring_case(text, "creatures nearby") != std::string_view::npos ||
               find_ignoring_case(text, "it contacts") != std::string_view::npos) {
        out.reach = SpellReach::Blast;
    }

    // Damage: a flat part after "does"/"damage is", and a scaling part when
    // "per point of skill" follows — either the same range (pure scaling) or
    // a second one after "plus".
    if (find_ignoring_case(text, "damage") == std::string_view::npos) {
        return out;
    }
    const std::size_t per_skill =
        std::min(find_ignoring_case(text, "per point of skill"),
                 find_ignoring_case(text, "per skill point"));
    for (const std::string_view phrase : {"does ", "damage is ", "damage is equal to "}) {
        std::size_t end = 0;
        const SpellRange flat = range_after(text, phrase, &end);
        if (flat.empty()) {
            continue;
        }
        if (per_skill == std::string_view::npos || per_skill < end) {
            out.damage = flat;
            break;
        }
        // Between the number and "per point of skill": a "plus" makes the
        // first range flat and the second scaling; none makes it scaling.
        const std::string_view between = text.substr(end, per_skill - end);
        if (const std::size_t plus = find_ignoring_case(between, "plus ");
            plus != std::string_view::npos) {
            out.damage = flat;
            out.damage_per_skill = range_after(between, "plus ");
        } else if (between.size() < 40) {
            out.damage_per_skill = flat;
        } else {
            out.damage = flat;  // the scaling phrase belongs to another sentence
        }
        break;
    }
    return out;
}

// What a cure spell lifts, spoken in its first sentence — "Cures poison",
// "Removes the afraid condition", "Automatically awakens" — mapped to the
// condition vocabulary the monster column writes, its own spellings
// ("Affraid") included.
struct SpellCure {
    bool poison = false;
    bool disease = false;
    std::string affliction;  // the column's spelling, e.g. "Affraid"

    [[nodiscard]] bool empty() const noexcept {
        return !poison && !disease && affliction.empty();
    }
};

[[nodiscard]] inline SpellCure parse_spell_cure(const SpellStatsEntry& spell) {
    using detail::find_ignoring_case;
    const std::string_view text = spell.description;
    SpellCure out;
    if (find_ignoring_case(text, "cures poison") != std::string_view::npos) {
        out.poison = true;
    } else if (find_ignoring_case(text, "cures disease") != std::string_view::npos) {
        out.disease = true;
    } else if (find_ignoring_case(text, "the afraid condition") != std::string_view::npos) {
        out.affliction = "Affraid";
    } else if (find_ignoring_case(text, "awakens") != std::string_view::npos &&
               find_ignoring_case(text, "sleep") != std::string_view::npos) {
        out.affliction = "Asleep";
    } else if (find_ignoring_case(text, "the cursed condition") != std::string_view::npos) {
        out.affliction = "Curse";
    } else if (find_ignoring_case(text, "the weak condition") != std::string_view::npos) {
        out.affliction = "Weak";
    } else if (find_ignoring_case(text, "cures paralysis") != std::string_view::npos) {
        out.affliction = "Paralyze";
    } else if (find_ignoring_case(text, "cures insanity") != std::string_view::npos) {
        out.affliction = "Insane";
    }
    return out;
}

// A duration written the rank cells' way: `"Duration 1 hour + 5 minutes
// per point of skill"`, or minutes alone, or hours alone.
struct SpellDuration {
    int base_minutes = 0;
    int per_skill_minutes = 0;

    [[nodiscard]] bool empty() const noexcept {
        return base_minutes <= 0 && per_skill_minutes <= 0;
    }
    [[nodiscard]] int minutes(int skill) const noexcept {
        return base_minutes + per_skill_minutes * skill;
    }
};

// Walk a passage that starts at its "duration" word, reading each number as
// hours or minutes by the word after it; a number before "per point of
// skill"/"per skill point" scales, the rest is base.
[[nodiscard]] inline SpellDuration parse_duration_text(std::string_view text) {
    using detail::find_ignoring_case;
    using detail::range_at;

    SpellDuration out;
    std::size_t p = 0;
    while (p < text.size()) {
        while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) == 0) {
            ++p;
        }
        if (p >= text.size()) {
            break;
        }
        std::size_t end = p;
        const SpellRange value = range_at(text, p, &end);
        const std::string_view rest = text.substr(end);
        int minutes = value.low;
        if (find_ignoring_case(rest.substr(0, 8), "week") != std::string_view::npos) {
            minutes *= 60 * 24 * 7;
        } else if (find_ignoring_case(rest.substr(0, 8), "day") != std::string_view::npos) {
            minutes *= 60 * 24;
        } else if (find_ignoring_case(rest.substr(0, 8), "hour") != std::string_view::npos ||
                   (rest.size() > 1 && rest.substr(0, 3) == " hr")) {
            minutes *= 60;
        }
        // The scaling phrase must belong to this number: look only up to
        // the next number in the cell.
        std::size_t next_digit = 0;
        while (next_digit < rest.size() &&
               std::isdigit(static_cast<unsigned char>(rest[next_digit])) == 0) {
            ++next_digit;
        }
        const std::string_view own = rest.substr(0, next_digit);
        const bool scales =
            find_ignoring_case(own, "per point of skill") != std::string_view::npos ||
            find_ignoring_case(own, "per skill point") != std::string_view::npos;
        if (scales) {
            out.per_skill_minutes += minutes;
        } else {
            out.base_minutes += minutes;
        }
        p = end;
    }
    return out;
}

[[nodiscard]] inline SpellDuration parse_spell_duration(const SpellStatsEntry& spell,
                                                        int mastery) {
    using detail::find_ignoring_case;

    const std::string_view rank = mastery >= 2   ? spell.master
                                  : mastery == 1 ? spell.expert
                                                 : spell.normal;
    if (const std::size_t at = find_ignoring_case(rank, "duration ");
        at != std::string_view::npos) {
        return parse_duration_text(rank.substr(at));
    }
    // Some spells state their time in the description instead: "The duration
    // of Mass Fear is 3 minutes per point of skill in Mind Magic". The same
    // walk reads it there, taken only when the scaling phrase is present so
    // a stray number in the prose is not mistaken for a clock.
    std::size_t in_text = find_ignoring_case(spell.description, "duration");
    if (in_text == std::string_view::npos) {
        in_text = find_ignoring_case(spell.description, "lasts");  // "lasts 1 hour per point"
    }
    if (in_text != std::string_view::npos &&
        find_ignoring_case(spell.description, "per point of skill") != std::string_view::npos) {
        SpellDuration from_text =
            parse_duration_text(std::string_view(spell.description).substr(in_text));
        // Only the scaling part is trusted from running prose; flat numbers
        // there are usually not times at all.
        from_text.base_minutes = 0;
        return from_text;
    }
    return {};
}

// One monster's spell, as `MONSTERS.TXT`'s own column writes it:
// `"Fireball,N,5"` — the spell's name, the mastery letter, and a real skill
// value, which is exactly what the per-skill dice scale by.
struct MonsterSpell {
    std::string name;
    int mastery = 0;  // N=0, E=1, M=2
    int skill = 0;

    [[nodiscard]] bool empty() const noexcept { return name.empty(); }
};

[[nodiscard]] inline MonsterSpell parse_monster_spell(std::string_view text) {
    MonsterSpell out;
    const std::size_t first = text.find(',');
    if (first == std::string_view::npos) {
        return out;
    }
    out.name = std::string(trim(text.substr(0, first)));
    const std::string_view rest = text.substr(first + 1);
    const std::size_t second = rest.find(',');
    const std::string_view mastery = trim(rest.substr(0, second));
    if (!mastery.empty()) {
        out.mastery = mastery[0] == 'M' || mastery[0] == 'm'   ? 2
                      : mastery[0] == 'E' || mastery[0] == 'e' ? 1
                                                               : 0;
    }
    if (second != std::string_view::npos) {
        out.skill = parse_int(rest.substr(second + 1), 0);
    }
    return out;
}

// Find a spell by the monster column's spelling of its name. The shipped
// column carries two typos — `"Dispell Magic"`, `"Psychic Shockt"` — so a
// name that is the other's prefix within two characters still finds it.
[[nodiscard]] inline const SpellStatsEntry* find_spell_tolerant(const SpellStatsTable& spells,
                                                                std::string_view name) {
    if (const auto* exact = spells.find(name); exact != nullptr) {
        return exact;
    }
    for (const auto& spell : spells.entries()) {
        const std::string_view have = spell.name;
        const std::string_view shorter = have.size() < name.size() ? have : name;
        const std::string_view longer = have.size() < name.size() ? name : have;
        if (longer.size() - shorter.size() <= 2 &&
            detail::same_ignoring_case(longer.substr(0, shorter.size()), shorter)) {
            return &spell;
        }
    }
    return nullptr;
}

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_SPELL_EFFECTS_HPP
