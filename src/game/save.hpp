#ifndef STARHAVEN_GAME_SAVE_HPP
#define STARHAVEN_GAME_SAVE_HPP

// Saving the game, and getting it back.
//
// The format is this engine's own and says so: a versioned, line-based text
// file of what the engine actually tracks — quest bits, event variables, the
// purse, the packs, the party, the clock, the map and where the party stands
// on it, and the current map's opened chests and thrown doors. It is not the
// original's save format and does not try to be; nothing here rereads or
// writes the original's files.

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "game/inventory.hpp"
#include "game/party.hpp"
#include "game/script_decorations.hpp"

namespace starhaven::game {

inline constexpr int kSaveVersion = 3;
inline constexpr int kOldestSaveVersion = 1;
inline constexpr const char* kSaveMagic = "starhaven-save";

// Everything a save holds, in plain values the shell assembles and applies.
struct SaveState {
    std::string map_file;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    std::int64_t minutes = 0;
    int gold = 0;
    int bank_gold = 0;
    int food = 0;

    // The hired help: enough to stand them back up through the profession
    // table, and when their wages next fall due.
    struct Hired {
        int npc_id = 0;
        int profession_id = 0;
        std::string name;
    };
    std::vector<Hired> hired;
    std::int64_t wage_day = 0;

    // The honors earned: filled Awards.txt rows.
    std::vector<int> awards;

    // Where Town Portal may reach: the outdoor towns seen, in first-visit
    // order, and when spell-borne flight wears off.
    std::vector<std::string> visited_towns;
    std::int64_t fly_until = 0;
    int reputation = 0;

    // What each member keeps readied for the cast key, the turn-based
    // toggle and the hourglass's count — appended for compatibility; an
    // older save simply reads them absent.
    std::array<int, 4> readied{};

    // The word at `+0x157c` on each character: what stays bound to the quick
    // key, as against what is readied now.
    std::array<int, 4> quick{};

    // The two counters beside the reputation: party `+0xe8` and `+0xf0`.
    int deaths = 0;
    int prison_terms = 0;
    bool turn_based = false;
    int hourglass_turn = 0;

    // Torch Light's hours, the eye, and Lloyd's markers: where, until when.
    std::int64_t torch_until = 0;
    std::int64_t eye_until = 0;
    int eye_rank = 0;
    struct Beacon {
        std::string map;
        float x = 0, y = 0, z = 0;
        std::int64_t until = 0;
    };
    std::vector<Beacon> beacons;
    std::set<int> bits;
    std::set<int> resolved_quests;
    std::map<std::string, std::set<int>> disabled_events;
    DecorationChanges decorations;
    std::map<int, int> variables;
    std::map<std::pair<int, int>, int> npc_topics;
    std::map<int, int> npc_places;
    std::set<int> autonotes;

    // What each map away from the party remembers: its opened chests,
    // its thrown doors, the actor slots whose monsters fell, and the day
    // it was left — the refill clock reads that day on return.
    struct RememberedMap {
        std::string file;
        std::int64_t day = 0;
        std::set<int> opened_chests;
        std::vector<std::uint32_t> open_doors;
        std::vector<std::size_t> dead;
    };
    std::vector<RememberedMap> remembered;
    std::array<Character, 4> party{};
    // The party's own sixteen buff slots; each character's sixteen ride
    // along inside the Character itself.
    PartyBuffs party_buffs{};
    std::array<std::vector<PackedItem>, 4> packs{};
    std::vector<int> opened_chests;
    std::vector<std::uint32_t> open_doors;
};

// Version 3 persists decoration changes; versions 1 and 2 remain readable.
// Version 2 added an end marker and explicit quest resolution history.
[[nodiscard]] std::string save_text(const SaveState& state);
// On failure, return false without modifying the caller's state.
[[nodiscard]] bool parse_save(std::string_view text, SaveState& out);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SAVE_HPP
