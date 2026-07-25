// Tests for the tab-separated design tables shipped in icons.lod.
//
// Hermetic: every fixture is synthesized from the format described in
// docs/formats/text-tables.md. No bytes are copied from a game archive.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;

namespace {

// Wrap a table body as a stored archive entry: 48-byte header, the unpacked
// size at 0x28, then a zlib stream.
std::vector<std::byte> make_entry(const std::string& body, bool compressed = true) {
    std::vector<std::byte> out(48, std::byte{0});
    const char* name = "TEST.TXT";
    std::memcpy(out.data(), name, std::strlen(name));

    const std::uint32_t unpacked = compressed ? static_cast<std::uint32_t>(body.size()) : 0;
    for (int i = 0; i < 4; ++i) {
        out[0x28 + static_cast<std::size_t>(i)] =
            static_cast<std::byte>((unpacked >> (8 * i)) & 0xFF);
    }

    if (!compressed) {
        for (const char c : body)
            out.push_back(static_cast<std::byte>(c));
        return out;
    }

    uLongf cap = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::uint8_t> z(cap);
    REQUIRE(compress(z.data(), &cap, reinterpret_cast<const Bytef*>(body.data()),
                     static_cast<uLong>(body.size())) == Z_OK);
    z.resize(cap);
    for (const std::uint8_t b : z)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

}  // namespace

TEST_CASE("splits rows on CRLF and fields on tabs", "[text_table]") {
    TextTable t;
    REQUIRE(TextTable::parse_body("a\tb\tc\r\n1\t2\t3\r\n", t) == TextTableError::None);
    REQUIRE(t.row_count() == 2);
    REQUIRE(t.cell(0, 0) == "a");
    REQUIRE(t.cell(1, 2) == "3");
}

TEST_CASE("a trailing row terminator does not add an empty row", "[text_table]") {
    TextTable t;
    REQUIRE(TextTable::parse_body("a\r\n", t) == TextTableError::None);
    REQUIRE(t.row_count() == 1);

    // A row left unterminated at the end is still a row.
    TextTable u;
    REQUIRE(TextTable::parse_body("a\r\nb", u) == TextTableError::None);
    REQUIRE(u.row_count() == 2);
    REQUIRE(u.cell(1, 0) == "b");
}

TEST_CASE("blank rows in the middle are kept", "[text_table]") {
    // The shipped tables carry rows of empty cells between sections; dropping
    // them would shift every row index after one.
    TextTable t;
    REQUIRE(TextTable::parse_body("a\tb\r\n\t\r\nc\td\r\n", t) == TextTableError::None);
    REQUIRE(t.row_count() == 3);
    REQUIRE(t.cell(1, 0).empty());
    REQUIRE(t.cell(2, 1) == "d");
}

TEST_CASE("quoted fields may contain tabs and newlines", "[text_table]") {
    TextTable t;
    REQUIRE(TextTable::parse_body("id\t\"two\tparts\nand a line break\"\tlast\r\n", t) ==
            TextTableError::None);
    REQUIRE(t.row_count() == 1);
    REQUIRE(t.rows()[0].size() == 3);
    REQUIRE(t.cell(0, 1) == "two\tparts\nand a line break");
    REQUIRE(t.cell(0, 2) == "last");
}

TEST_CASE("a doubled quote inside a quoted field is one quote", "[text_table]") {
    TextTable t;
    REQUIRE(TextTable::parse_body("\"\"\"Die Intruder!\"\" scroll\"\r\n", t) ==
            TextTableError::None);
    REQUIRE(t.cell(0, 0) == "\"Die Intruder!\" scroll");
}

TEST_CASE("an unterminated quote is rejected", "[text_table]") {
    TextTable t;
    REQUIRE(TextTable::parse_body("a\t\"never closed\r\n", t) == TextTableError::UnterminatedQuote);
    REQUIRE(t.row_count() == 0);
}

TEST_CASE("rows may be ragged", "[text_table]") {
    // Trailing columns are present on some rows and absent on others, so
    // reading past a row's end must be safe rather than undefined.
    TextTable t;
    REQUIRE(TextTable::parse_body("a\tb\tc\r\nd\r\n", t) == TextTableError::None);
    REQUIRE(t.cell(1, 2).empty());
    REQUIRE(t.cell(9, 0).empty());
}

TEST_CASE("numbers tolerate padding and thousands separators", "[text_table]") {
    REQUIRE(parse_int(" 93 ") == 93);
    REQUIRE(parse_int("\" 1,300 \"", -7) == -7);  // the quotes are not stripped here
    REQUIRE(parse_int(" 1,300 ") == 1300);
    REQUIRE(parse_int("-12") == -12);

    // Codes that merely start with digits must not read as numbers.
    REQUIRE(parse_int("2-4", -1) == -1);
    REQUIRE(parse_int("3D6+2", -1) == -1);
    REQUIRE(parse_int("5%6D20+L2Bow", -1) == -1);
    REQUIRE(parse_int("", -1) == -1);
}

TEST_CASE("cp1252 converts to UTF-8", "[text_table]") {
    // 0x92 is a right single quote in cp1252, not U+0092.
    REQUIRE(cp1252_to_utf8("Hermit\x92s") == "Hermit’s");
    REQUIRE(cp1252_to_utf8("caf\xE9") == "café");
    // 0x81 is unassigned; it becomes the replacement character rather than
    // inventing a letter.
    REQUIRE(cp1252_to_utf8("\x81") == "�");
    REQUIRE(cp1252_to_utf8("plain") == "plain");
}

TEST_CASE("unwraps the archive container", "[text_table]") {
    const auto e = make_entry("a\tb\r\n1\t2\r\n");
    TextTable t;
    REQUIRE(TextTable::parse(e, t) == TextTableError::None);
    REQUIRE(t.name() == "TEST.TXT");
    REQUIRE(t.row_count() == 2);
    REQUIRE(t.cell(1, 1) == "2");
}

TEST_CASE("an entry declaring no unpacked size is stored as-is", "[text_table]") {
    // One shipped table (errorlog.txt) is stored uncompressed this way.
    const auto e = make_entry("plain\ttext\r\n", /*compressed=*/false);
    TextTable t;
    REQUIRE(TextTable::parse(e, t) == TextTableError::None);
    REQUIRE(t.cell(0, 1) == "text");
}

TEST_CASE("a container whose declared size disagrees is rejected", "[text_table]") {
    auto e = make_entry("a\tb\r\n");
    e[0x28] = static_cast<std::byte>(0xFF);  // claim a length the stream cannot produce
    TextTable t;
    REQUIRE(TextTable::parse(e, t) == TextTableError::SizeMismatch);
}

TEST_CASE("a truncated container is rejected", "[text_table]") {
    TextTable t;
    const std::vector<std::byte> tiny(10, std::byte{0});
    REQUIRE(TextTable::parse(tiny, t) == TextTableError::TooSmall);

    auto e = make_entry("a\r\n");
    e.resize(50);  // keep the header, cut the stream
    REQUIRE(TextTable::parse(e, t) == TextTableError::InflateFailed);
}

namespace {

// A MapStats.txt shaped fixture: two rows of merged headings, the real header,
// then data. Column positions match the shipped table.
std::string map_stats_body() {
    std::string s;
    s += "\tMap Stats\t\t\r\n";
    s += "\t\t\tReset\r\n";
    s += "#\tName\tFile name\t#\tDay\tDays\t0-10\t0-10\t0-6\t%\t%\t%\t%\tMon1 Pic\tMon 1\t 1-5\t#"
         "\tMon2 Pic\tMon 2\t 1-5\t#\tMon3 Pic\tMon 3\t 1-5\t#\tTrack\tMap Designer\r\n";
    s += "1\tSweet Water\tOutA1.Odm\t0\t0\t224\t8\t9\t6\t40\t50\t50\t0\tDemon\tDevil Spawn\t3"
         "\t 2-4\tDemonFly\tDevil Captain\t3\t 2-4\t0\t0\t1\t 1-4\t5\tPeter\r\n";
    s += "2\tpending\tzddb02.blv\t0\t0\t168\t7\t8\t6\t20\t40\t30\t30\t0\t0\t1\t 1-4\t0\t0\t1"
         "\t 1-4\t0\t0\t1\t 1-4\t7\t\r\n";
    s += "\t\t\r\n";  // the block of blank rows the table ends with
    return s;
}

}  // namespace

TEST_CASE("MapStats rows parse into typed entries", "[map_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(map_stats_body(), table) == TextTableError::None);
    MapStatsTable maps;
    REQUIRE(MapStatsTable::parse(table, maps) == MapStatsError::None);

    // The blank trailing row is not a map.
    REQUIRE(maps.size() == 2);

    const MapStatsEntry& first = maps.entries()[0];
    REQUIRE(first.id == 1);
    REQUIRE(first.name == "Sweet Water");
    REQUIRE(first.file_name == "OutA1.Odm");
    REQUIRE(first.refill_days == 224);
    REQUIRE(first.encounter_percent == 40);
    REQUIRE(first.music_track == 5);
    REQUIRE(first.designer == "Peter");
    REQUIRE(first.monsters[0].monster == "Devil Spawn");
    REQUIRE(first.monsters[0].count == "2-4");  // the leading space is trimmed
    REQUIRE(first.monsters[2].empty());
    REQUIRE_FALSE(first.placeholder());
    REQUIRE(maps.entries()[1].placeholder());
}

TEST_CASE("maps are found regardless of case", "[map_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(map_stats_body(), table) == TextTableError::None);
    MapStatsTable maps;
    REQUIRE(MapStatsTable::parse(table, maps) == MapStatsError::None);

    // The table writes "OutA1.Odm" where the archive holds "outa1.odm".
    REQUIRE(maps.find("outa1.odm") != nullptr);
    REQUIRE(maps.find("OUTA1.ODM")->music_track == 5);
    REQUIRE(maps.find("nosuch.odm") == nullptr);
}

TEST_CASE("a table without the expected header is rejected", "[map_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("something\telse\r\n1\t2\r\n", table) == TextTableError::None);
    MapStatsTable maps;
    REQUIRE(MapStatsTable::parse(table, maps) == MapStatsError::NoHeader);
}

namespace {

std::string monsters_body() {
    std::string s;
    s += "Default Monsters\t\t\r\n";
    s += "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
         "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
         "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    s += "\tA\t\t0\t\r\n";  // the row of defaults that follows the header
    s += "1\tArcherA\tArcher\t9\t35\t14\t171\t5%3D20+L1Bow\t0\tN\tShort\tNormal\t4\t140\t90\t0\t0"
         "\tPhys\t1D6+1\tArrow\t0\t0\t0\t0\t0\t0\t10\t10\t10\t10\t0\t0\t0\r\n";
    s += "2\tDragonCave A\tFire Lizard\t40\t280\t40\t\" 2,000 \"\t0\t0\tY\tMed\tAggress\t4\t200"
         "\t70\t0\t0\tFire\t10D6\tFire\t0\t0\t0\t0\t0\t0\tImm\t30\t30\t30\t20\t30\t0\r\n";
    return s;
}

}  // namespace

TEST_CASE("MONSTERS rows parse into typed entries", "[monster_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(monsters_body(), table) == TextTableError::None);
    MonsterStatsTable monsters;
    REQUIRE(MonsterStatsTable::parse(table, monsters) == MonsterStatsError::None);

    // The defaults row has no id, so it is not a monster.
    REQUIRE(monsters.size() == 2);

    const MonsterStatsEntry& archer = monsters.entries()[0];
    REQUIRE(archer.picture == "ArcherA");
    REQUIRE(archer.name == "Archer");
    REQUIRE(archer.level == 9);
    REQUIRE(archer.hit_points == 35);
    REQUIRE(archer.armor_class == 14);
    REQUIRE(archer.experience == 171);
    REQUIRE_FALSE(archer.flying);
    REQUIRE(archer.attacks[0].type == "Phys");
    REQUIRE(archer.attacks[0].damage == "1D6+1");
    REQUIRE(archer.attacks[0].missile == "Arrow");
    REQUIRE(archer.resistance(Resistance::Fire) == 10);

    const MonsterStatsEntry& lizard = monsters.entries()[1];
    REQUIRE(lizard.flying);
    REQUIRE(lizard.experience == 2000);  // written " 2,000 "
    REQUIRE(lizard.resistance(Resistance::Fire) == kResistanceImmune);
    REQUIRE(lizard.resistance(Resistance::Physical) == 20);
}

TEST_CASE("monster names join across case and spacing", "[monster_stats]") {
    // MONSTERS.TXT and DMONLIST.BIN disagree on five names purely in
    // typography, so the lookup normalizes both.
    TextTable table;
    REQUIRE(TextTable::parse_body(monsters_body(), table) == TextTableError::None);
    MonsterStatsTable monsters;
    REQUIRE(MonsterStatsTable::parse(table, monsters) == MonsterStatsError::None);

    REQUIRE(monsters.find("DragonCaveA") != nullptr);
    REQUIRE(monsters.find("dragoncave a")->name == "Fire Lizard");
    REQUIRE(monsters.find("Dragon Cave B") == nullptr);
    REQUIRE(normalize_picture("PeasantF1C") == normalize_picture("Peasantf1C"));
}
