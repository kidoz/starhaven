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
#include <cstdint>
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

// The side panel each interior wears: the same executable table that
// names the videos carries it. The records are 16 bytes at `0x4be88c` —
// byte 0 the `EVPAN###` panel number, a dword at +4, the byte at +8 a
// kind, and the pointer at +12 the video name in exactly the order
// above. All 52 distinct panel numbers ship in `icons.lod`. `observed`
// Reproduce the join with `data_info --backdrops`.
inline constexpr std::array<std::uint8_t, 118> kInteriorPanels{
    4, 22, 13, 23, 10, 14, 42, 30, 7, 36, 14, 12,
    11, 16, 41, 14, 30, 16, 25, 9, 34, 19, 18, 38,
    13, 15, 21, 36, 13, 20, 36, 31, 18, 30, 32, 24,
    49, 24, 49, 20, 49, 17, 33, 55, 6, 33, 15, 49,
    37, 35, 36, 39, 39, 39, 28, 27, 29, 26, 43, 24,
    24, 24, 25, 38, 25, 18, 8, 3, 13, 2, 36, 36,
    1, 15, 9, 41, 30, 24, 22, 36, 13, 40, 44, 45,
    24, 22, 8, 53, 54, 52, 24, 49, 13, 46, 25, 25,
    30, 51, 25, 25, 47, 49, 13, 20, 49, 49, 20, 51,
    49, 19, 14, 49, 25, 49, 36, 55, 55, 55};

// The dword at +4, kept as measured and read as nothing: it is **not** a
// `DSOUNDS` id (none of 500, 501, 505, 513, 532, 549 resolves in the
// sound table) and **not** a `GLOBAL.TXT` row (500 there is "You have
// %lu gold"). Its values cluster oddly — the shops carry 500..518, the
// taverns and temples carry ids that read as town names in another
// table — and what it indexes is `unknown`.
inline constexpr std::array<std::uint16_t, 118> kInteriorField4{
    500, 505, 506, 507, 501, 502, 503, 516, 517, 518, 513, 514,
    515, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    175, 530, 20, 297, 358, 552, 550, 549, 548, 551, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 504, 385, 72, 0,
    533, 534, 535, 519, 520, 521, 510, 509, 508, 511, 512, 332,
    91, 0, 260, 61, 549, 256, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 545, 546, 547, 532, 532, 532,
    532, 532, 532, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// The panel a Picture value wears, or 0 when out of the table's reach.
[[nodiscard]] inline int interior_panel(int picture) noexcept {
    if (picture < 1 || picture > static_cast<int>(kInteriorPanels.size())) {
        return 0;
    }
    return kInteriorPanels[static_cast<std::size_t>(picture) - 1];
}

// The video a Picture value names, or empty when out of the table's reach.
[[nodiscard]] inline std::string_view interior_video(int picture) noexcept {
    if (picture < 1 || picture > static_cast<int>(kInteriorVideos.size())) {
        return {};
    }
    return kInteriorVideos[static_cast<std::size_t>(picture) - 1];
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INTERIORS_HPP
