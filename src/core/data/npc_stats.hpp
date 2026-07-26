#ifndef STARHAVEN_CORE_DATA_NPC_STATS_HPP
#define STARHAVEN_CORE_DATA_NPC_STATS_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// One row of `npcprof.txt`: what an NPC does for a living, and what hiring one
// costs and gives.
struct NpcProfessionEntry {
    int id = 0;  // 1-based; an NPC's profession column indexes this
    std::string name;
    int random_chance = 0;    // headed "Random Chance"
    int hire_cost = 0;        // headed "Join Cost/w"
    std::string personality;  // "Merchant", "Sorcerer", "Scholar", ...
    std::string action_text;
    std::string party_benefit;
    std::string join_text;
};

// One row of `NPCdata.txt`: a named person the game places somewhere.
struct NpcEntry {
    int id = 0;
    std::string name;
    int picture = 0;
    int state = 0;
    int fame = 0;
    int reputation = 0;

    // The establishment this NPC stands in, as a `2DEvents.txt` row id. Zero
    // where the cell is blank and **-1** on eighteen rows, which is the
    // table's way of saying the person is not in one; the value is kept as
    // written and `placed()` is the question worth asking.
    int building_id = 0;

    // The profession, as an `npcprof.txt` row id, or 0.
    int profession_id = 0;

    bool can_join = false;
    bool has_news = false;
    std::array<int, 3> events{};  // event ids the notes describe

    std::string notes;

    // Whether this person stands in an establishment at all.
    [[nodiscard]] bool placed() const noexcept { return building_id > 0; }
};

// One thing an NPC can be asked about. `npctopic.txt` and `npctext.txt` share
// a single numbering — the file headings call it "Text Number From NPC
// Events.Doc" — so a topic is its label and its words together. An NPC's three
// event columns index this.
struct NpcDialogueEntry {
    int id = 0;
    std::string topic;  // the short label, e.g. "The Letter"
    std::string text;   // what is said; empty on 22 of the 493 topics
};

// One regional rumour from `NPCNews.txt`.
struct NpcNewsEntry {
    int id = 0;
    // The table heads this "Map", but its sixteen values do not line up with
    // the map list; see docs/formats/text-tables.md. Kept as written.
    int map_value = 0;
    std::string topic;
    std::string text;
};

enum class NpcStatsError : std::uint8_t {
    None,
    // No row carries the expected header.
    NoHeader,
};

// `NPCdata.txt`, parsed into rows.
class NpcTable {
public:
    NpcTable() = default;

    [[nodiscard]] static NpcStatsError parse(const TextTable& table, NpcTable& out);

    [[nodiscard]] const std::vector<NpcEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Everyone standing in one establishment, by its `2DEvents.txt` row id.
    [[nodiscard]] std::vector<const NpcEntry*> in_building(int building_id) const;

private:
    std::vector<NpcEntry> entries_;
};

// `npcprof.txt`, parsed into rows.
class NpcProfessionTable {
public:
    NpcProfessionTable() = default;

    [[nodiscard]] static NpcStatsError parse(const TextTable& table, NpcProfessionTable& out);

    [[nodiscard]] const std::vector<NpcProfessionEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Resolve a profession id. Returns nullptr when the table has no such row.
    [[nodiscard]] const NpcProfessionEntry* at(int id) const noexcept;

private:
    std::vector<NpcProfessionEntry> entries_;
};

// `npctopic.txt` and `npctext.txt`, merged on their shared numbering.
class NpcDialogueTable {
public:
    NpcDialogueTable() = default;

    // Both tables are needed: the topics carry the labels, the texts the
    // words, and an id may appear in the first without the second.
    [[nodiscard]] static NpcStatsError parse(const TextTable& topics, const TextTable& texts,
                                             NpcDialogueTable& out);

    [[nodiscard]] const std::vector<NpcDialogueEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const NpcDialogueEntry* at(int id) const noexcept;

private:
    std::vector<NpcDialogueEntry> entries_;
};

// `NPCNews.txt`.
class NpcNewsTable {
public:
    NpcNewsTable() = default;

    [[nodiscard]] static NpcStatsError parse(const TextTable& table, NpcNewsTable& out);

    [[nodiscard]] const std::vector<NpcNewsEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<NpcNewsEntry> entries_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_NPC_STATS_HPP
