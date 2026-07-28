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

#include "core/data/npc_stats.hpp"
#include "core/data/profession_text.hpp"
#include "core/world/map_session.hpp"
#include "game/clock.hpp"

namespace starhaven::game {

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
[[nodiscard]] inline Conversation talk_to(const world::SessionNpc& person,
                                          const data::NpcDialogueTable& dialogue,
                                          const data::NpcPersonalityTable& personalities,
                                          const data::ProfessionTextTable& trade_talk,
                                          const GameClock& clock) {
    Conversation out;
    out.who = person.name;
    if (!person.profession.empty()) {
        out.who += ", " + data::cp1252_to_utf8(person.profession);
    }

    // The personality's opening line, and which approaches it entertains.
    if (const auto* personality = personalities.find(person.personality); personality != nullptr) {
        out.greeting = data::cp1252_to_utf8(std::string(personality->message(1)));
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
