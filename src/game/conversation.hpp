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

// Who is speaking to whom, for the placeholders every line of NPC prose
// carries. What each number stands for is read from the lines themselves:
// `"I'm %01"` and `"Name's %01"` make 1 the speaker, `"%06 %02"` makes 2 the
// person addressed and 6 an honorific. `inferred`
struct Speech {
    std::string speaker;   // %01
    std::string listener;  // %02
    bool listener_is_female = false;
    int hour = 12;
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
        std::string_view listener = {}, bool listener_is_female = false) {
    const Speech who{person.name, std::string(listener), listener_is_female, clock.hour()};
    Conversation out;
    out.who = person.name;
    if (!person.profession.empty()) {
        out.who += ", " + data::cp1252_to_utf8(person.profession);
    }

    // The personality's opening line, and which approaches it entertains.
    if (const auto* personality = personalities.find(person.personality); personality != nullptr) {
        out.greeting =
            substitute(data::cp1252_to_utf8(std::string(personality->message(1))), who, words);
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
