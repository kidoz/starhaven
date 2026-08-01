#ifndef STARHAVEN_GAME_CONVERSATION_HPP
#define STARHAVEN_GAME_CONVERSATION_HPP

// Talking to the people the design tables place in establishments.
//
// Five decoded tables meet here and none of it is invented: who is where from
// `NPCdata.txt`, what they do from `npcprof.txt`, what their trade talks about
// today from `PROFTEXT.txt`, what they can be asked about from `npctopic.txt`
// and `npctext.txt`, and how they take being begged, bribed or threatened from
// `npcbtb.txt`. See docs/formats/text-tables.md.

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <cctype>
#include <string_view>

#include "core/data/interface_strings.hpp"
#include "core/data/npc_stats.hpp"
#include "core/data/profession_text.hpp"
#include "core/world/map_session.hpp"
#include "game/clock.hpp"

namespace starhaven::game {

// The words `%05` and `%06` stand for, by their `GLOBAL.TXT` ids. The three
// times of day are consecutive there, and the honorifics sit five apart, which
// is what identifies them: "Good %05!" can only be "Good morning", "Good day"
// or "Good evening". `observed`
inline constexpr int kStringMorning = 395;
inline constexpr int kStringDay = 396;
inline constexpr int kStringEvening = 397;
inline constexpr int kStringSir = 385;
inline constexpr int kStringLady = 387;

// The rest of the phrasebook's neighborhood, censused: his and her at 383
// and 384, Lord and Lady at 388 and 389, son and daughter at 392 and 393.
// "%09 band" reads as the listener's possessive, "my %10" as the
// Lord/Lady address, "Ah, %16" as the familiar child-word — all pinned by
// their sentences; %03 is the speaker's own possessive, left visible
// until the speaker's gender is modelled. `inferred`
inline constexpr int kStringHis = 383;
inline constexpr int kStringHer = 384;
inline constexpr int kStringLord = 388;
inline constexpr int kStringLadyTitle = 389;
inline constexpr int kStringSon = 392;
inline constexpr int kStringDaughter = 393;

// Who is speaking to whom, for the placeholders every line of NPC prose
// carries. What each number stands for is read from the lines themselves:
// `"I'm %01"` and `"Name's %01"` make 1 the speaker, `"%06 %02"` makes 2 the
// person addressed and 6 an honorific. `inferred`
struct Speech {
    std::string speaker;   // %01
    std::string listener;  // %02
    bool listener_is_female = false;
    int hour = 12;

    // At a counter: what is being handled and for how much. `"This %24 is of
    // the finest quality... for %27 gold"` and `"Ordinarily I sell things like
    // this %24 for %25"` are what name them. `inferred`
    std::string item;   // %24
    std::string title;  // %28, the shopkeeper's own trade
    int asking = 0;     // %25, the ordinary price
    int offered = 0;    // %27, the one actually named

    // Pinned by the census of every %NN across the shipped prose: "Your
    // reputation is %11" names the standing's own word, "takes %17
    // percent" the profession's cut, "what it is for %29 gold" the
    // identify price. `inferred` from the sentences around them.
    std::string reputation;  // %11
    int percent = 0;         // %17
    int naming = 0;          // %29
};

// Replace the placeholders a line carries. Anything not known is left as it
// stands rather than blanked, so an unrecognised code is visible instead of
// silently swallowing text.
[[nodiscard]] inline std::string substitute(std::string_view text, const Speech& who,
                                            const data::InterfaceStrings& words) {
    const auto word = [&words](int id) { return data::cp1252_to_utf8(std::string(words.at(id))); };
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 2 >= text.size() ||
            std::isdigit(static_cast<unsigned char>(text[i + 1])) == 0 ||
            std::isdigit(static_cast<unsigned char>(text[i + 2])) == 0) {
            out.push_back(text[i]);
            continue;
        }
        const int code = (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
        std::string with;
        switch (code) {
        case 1:
            with = who.speaker;
            break;
        case 2:
            with = who.listener;
            break;
        case 5:
            with = who.hour < 12 ? word(kStringMorning)
                                 : (who.hour < 18 ? word(kStringDay) : word(kStringEvening));
            break;
        case 6:
            with = who.listener_is_female ? word(kStringLady) : word(kStringSir);
            break;
        case 24:
            with = who.item;
            break;
        case 25:
            with = who.asking > 0 ? std::to_string(who.asking) : std::string{};
            break;
        case 27:
            with = who.offered > 0 ? std::to_string(who.offered) : std::string{};
            break;
        case 28:
            with = who.title;
            break;
        case 11:
            with = who.reputation;
            break;
        case 17:
            with = who.percent > 0 ? std::to_string(who.percent) : std::string{};
            break;
        case 29:
            with = who.naming > 0 ? std::to_string(who.naming) : std::string{};
            break;
        case 9:
            with = word(who.listener_is_female ? kStringHer : kStringHis);
            break;
        case 10:
            with = word(who.listener_is_female ? kStringLadyTitle : kStringLord);
            break;
        case 16:
            with = word(who.listener_is_female ? kStringDaughter : kStringSon);
            break;
        default:
            break;
        }
        if (with.empty()) {
            out.push_back(text[i]);  // leave an unknown code visible
            continue;
        }
        out += with;
        i += 2;
    }
    return out;
}

// How the party stands in the world's eyes when a greeting is chosen. The
// bands are this engine's own — the table names them ("Rep notorious",
// "Rep below zero", "Rep above 10", "Rep saintly", "Fame too low") without
// numbers — and say so: notorious at -50, saintly at +50, fame wanted at 5.
// `inferred`
struct Standing {
    int reputation = 0;
    int fame = 0;
    bool met_before = false;
};

// **The scale, traced.** Party `+0xd8` — the global at `0x908d48` — is the
// reputation, and three instructions give it its span:
//
//   * `0x403778` takes **50** off it every time an actor dies, once per
//     death and unconditionally, right where the death animation is set;
//   * `0x403086` takes **100** off it somewhere else;
//   * `0x43c598` tests it against **-1000** and, when it is at or past that,
//     resets it, counts the occasion at `0x908d60` and grants award **83** to
//     all four characters — which `Awards.txt` reads "Served %u Prison
//     Terms".
//
// So the bad end of the scale is -1000, prison is what waits there, and a
// death is a twentieth of the way. `observed`
inline constexpr int kReputationPerDeath = 50;
inline constexpr int kReputationSecondPenalty = 100;
inline constexpr int kReputationJail = -1000;
inline constexpr int kPrisonAward = 83;

// The bands the table names without numbers are still this engine's, but they
// are now scaled to the traced span rather than to nothing: half way to
// prison, and as far the other way. `inferred`
inline constexpr int kNotoriousAt = kReputationJail / 2;
inline constexpr int kSaintlyAt = -kReputationJail / 2;
inline constexpr int kFameWanted = 5;

// What a death costs and whether it lands the party in prison. Returns true
// when the reputation reached the bound, having reset it as the original
// does.
[[nodiscard]] inline bool reputation_after_death(int& reputation) noexcept {
    reputation -= kReputationPerDeath;
    if (reputation > kReputationJail) {
        return false;
    }
    reputation = 0;
    return true;
}

// Which npcbtb message number greets this standing: the table's own ladder,
// most specific first, falling back to the plain greeting where a
// personality has no wording for a rung.
[[nodiscard]] inline int greeting_number(const data::NpcPersonality& personality,
                                         const Standing& standing) {
    const auto has = [&personality](int number) {
        return !personality.message(number).empty();
    };
    if (standing.fame < kFameWanted && has(6)) {
        return 6;
    }
    if (standing.reputation <= kNotoriousAt && (has(7) || has(8))) {
        return has(7) ? 7 : 8;
    }
    if (standing.reputation >= kSaintlyAt && (has(9) || has(10))) {
        return has(9) ? 9 : 10;
    }
    if (standing.reputation < 0) {
        const int number = standing.met_before ? 15 : 11;
        if (has(number)) {
            return number;
        }
    }
    if (standing.reputation > 10) {
        const int number = standing.met_before ? 16 : 12;
        if (has(number)) {
            return number;
        }
    }
    return standing.met_before && has(2) ? 2 : 1;
}

// What one person has to say right now.
struct Conversation {
    std::string who;                      // "Caine, Blacksmith"
    std::string greeting;                 // their personality's first line
    std::string today;                    // what their trade talks about on this weekday
    std::vector<std::string> topics;      // what they can be asked about
    std::vector<std::string> approaches;  // which of beg, bribe and threat work
};

// Build it. Anything a table does not have is simply left out rather than
// filled in.
[[nodiscard]] inline Conversation
talk_to(const world::SessionNpc& person, const data::NpcDialogueTable& dialogue,
        const data::NpcPersonalityTable& personalities, const data::ProfessionTextTable& trade_talk,
        const GameClock& clock, const data::InterfaceStrings& words = {},
        std::string_view listener = {}, bool listener_is_female = false,
        const Standing& standing = {}) {
    Speech who{person.name, std::string(listener), listener_is_female, clock.hour()};
    // The standing's word, for the greetings that name it.
    who.reputation = standing.reputation <= kNotoriousAt ? "notorious"
                     : standing.reputation < 0           ? "poor"
                     : standing.reputation >= kSaintlyAt ? "saintly"
                     : standing.reputation > 10          ? "respectable"
                                                         : "average";
    Conversation out;
    out.who = person.name;
    if (!person.profession.empty()) {
        out.who += ", " + data::cp1252_to_utf8(person.profession);
    }

    // The personality's opening line, and which approaches it entertains.
    if (const auto* personality = personalities.find(person.personality); personality != nullptr) {
        out.greeting = substitute(
            data::cp1252_to_utf8(std::string(
                personality->message(greeting_number(*personality, standing)))),
            who, words);
        static constexpr std::array<std::pair<data::NpcApproach, const char*>, 3> kApproaches{
            {{data::NpcApproach::Beg, "beg"},
             {data::NpcApproach::Bribe, "bribe"},
             {data::NpcApproach::Threat, "threaten"}}};
        for (const auto& [approach, word] : kApproaches) {
            if (personality->allows_approach(approach)) {
                out.approaches.emplace_back(word);
            }
        }
    }

    // What the trade is talking about on this day of the week.
    if (const auto* said = trade_talk.at(person.profession_id); said != nullptr) {
        const auto day = static_cast<std::size_t>(clock.day() % 7);
        if (day < said->days.size() && !said->days[day].topic.empty()) {
            out.today = data::cp1252_to_utf8(said->days[day].topic);
        }
    }

    // And the three things this particular person can be asked about.
    for (const int id : person.topics) {
        const auto* entry = dialogue.at(id);
        if (entry == nullptr || entry->topic.empty()) {
            continue;
        }
        out.topics.push_back(data::cp1252_to_utf8(entry->topic));
    }
    return out;
}

// Which dialogue id the chosen topic is. Topic id N, `npctext.txt` row N and
// `GLOBAL.EVT` event N share one id space — the label, the prose and the
// logic — so this is also the quest event a topic runs when the global
// script defines it. Returns 0 when the choice names nothing.
[[nodiscard]] inline int topic_id(const world::SessionNpc& person,
                                  const data::NpcDialogueTable& dialogue, std::size_t which) {
    std::size_t seen = 0;
    for (const int id : person.topics) {
        const auto* entry = dialogue.at(id);
        if (entry == nullptr || entry->topic.empty()) {
            continue;
        }
        if (seen == which) {
            return id;
        }
        ++seen;
    }
    return 0;
}

// What a topic's answer is, for when one is chosen.
[[nodiscard]] inline std::string topic_answer(const world::SessionNpc& person,
                                              const data::NpcDialogueTable& dialogue,
                                              std::size_t which) {
    std::size_t seen = 0;
    for (const int id : person.topics) {
        const auto* entry = dialogue.at(id);
        if (entry == nullptr || entry->topic.empty()) {
            continue;
        }
        if (seen == which) {
            return data::cp1252_to_utf8(entry->text);
        }
        ++seen;
    }
    return {};
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_CONVERSATION_HPP
