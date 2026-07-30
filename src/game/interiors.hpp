#ifndef STARHAVEN_GAME_INTERIORS_HPP
#define STARHAVEN_GAME_INTERIORS_HPP

// The interior each establishment shows, by 2DEvents' Picture column.
//
// The order is the game's own: MM6.exe's EVENTS.CPP string block lists the
// `Anims*.vid` video names contiguously, and read in descending address
// order from "blcksrch" they number exactly 1..118. Every anchor agrees —
// the smithies under the weapon shops, each school's guild on its own
// screen, the P/M/R houses on roompor/roommid/roomrch, the Seer in the
// poor oracle's hut, the twenty dungeon mouths on d01..d20. `observed`
// for the strings and anchors, `inferred` for the descending read.
// Reproduce with `data_info --backdrops`; see docs/formats/text-tables.md.

#include <array>
#include <string_view>

namespace starhaven::game {

inline constexpr std::array<std::string_view, 118> kInteriorVideos{
    "blcksrch", "Blcksmid", "blcksPor", "Apthcrch", "Apthcmid", "Apthcwch",
    "magrch",   "magmid",   "magicpor", "genstrch", "genstmid", "genstpor",
    "Cityrich", "Citymid",  "CityPoor", "CitySpec", "Citytrtr", "throne06",
    "throne03", "throne02", "throne01", "throne05", "throne04", "tavpoor1",
    "tavrich",  "tavpoor2", "TavMid",   "tavpirat", "tavgob",   "temppoor",
    "tempmid",  "temprich", "tempevil", "tempruin", "t7",       "t6",
    "t1",       "t4",       "t5",       "t8",       "oracrich", "oracpoor",
    "circus1",  "Bank",     "stables",  "ship",     "jail",     "thfrich",
    "thfpoor",  "thfpirat", "mercrich", "mercmid",  "mercpoor", "elemFire",
    "elemerth", "elemair",  "elemwatr", "elemall",  "mirpthl",  "mirpthd",
    "mirpthdl", "selfspir", "selfmind", "selfbody", "selfall",  "roompor1",
    "roompor2", "roompor3", "roompor4", "roommid1", "roommid2", "roommid3",
    "roommid4", "roomrch1", "roomrch2", "roomrch3", "roomrch4", "ArmRich",
    "Armmid",   "Armpoor",  "train1",   "train2",   "train3",   "train4",
    "train5",   "train6",   "Pyramid",  "hive",     "d14",      "d06",
    "d16",      "d05",      "d15",      "d13",      "d17",      "d03",
    "d09",      "d12",      "t2",       "t3",       "d10",      "d11",
    "d02",      "d04",      "d18",      "d19",      "d07",      "d20",
    "d08",      "CstlGood", "d01",      "cd1",      "cd2",      "cd3",
    "circus2",  "statue",   "archloop", "noarchie"};

// The video a Picture value names, or empty when out of the table's reach.
[[nodiscard]] inline std::string_view interior_video(int picture) noexcept {
    if (picture < 1 || picture > static_cast<int>(kInteriorVideos.size())) {
        return {};
    }
    return kInteriorVideos[static_cast<std::size_t>(picture) - 1];
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INTERIORS_HPP
