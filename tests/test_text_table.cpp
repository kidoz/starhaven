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

#include "core/data/item_generation.hpp"
#include "core/data/item_stats.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/text_table.hpp"

using namespace starhaven::data;
using starhaven::Mm6Random;

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

namespace {

std::string items_body() {
    std::string s;
    s += "Items\r\n";
    s += "Item #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
         "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    s += "0\tblank\t\t0\t0\t0\t0\t0\t0\t0\t\t0\t0\t0\t0\tplaceholder\r\n";
    s += "1\tsword01\tLong Sword\t100\tAttack\tSword\tMight\t2\tArtifact\t3\tFine blade"
         "\t42\t2\t11\t12\tfixture note\r\n";
    return s;
}

}  // namespace

TEST_CASE("ITEMS rows parse into direct-id-addressable entries", "[item_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(items_body(), table) == TextTableError::None);
    ItemStatsTable items;
    REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::None);
    REQUIRE(items.size() == 2);

    const ItemStatsEntry* placeholder = items.at(0);
    REQUIRE(placeholder != nullptr);
    REQUIRE(placeholder->name.empty());

    const ItemStatsEntry* sword = items.at(1);
    REQUIRE(sword != nullptr);
    REQUIRE(sword->picture == "sword01");
    REQUIRE(sword->name == "Long Sword");
    REQUIRE(sword->value == 100);
    REQUIRE(sword->equip_stat == "Attack");
    REQUIRE(sword->equip_type == ItemEquipType::Other);
    REQUIRE(sword->skill_group == "Sword");
    REQUIRE(sword->modifier_1 == "Might");
    REQUIRE(sword->modifier_2 == 2);
    REQUIRE(sword->material == "Artifact");
    REQUIRE(sword->id_rep_st == 3);
    REQUIRE(sword->unidentified_name == "Fine blade");
    REQUIRE(sword->sprite_index == 42);
    REQUIRE(sword->shape == 2);
    REQUIRE(sword->equip_x == 11);
    REQUIRE(sword->equip_y == 12);
    REQUIRE(sword->notes == "fixture note");
    REQUIRE(items.at(2) == nullptr);
}

TEST_CASE("ITEMS equip labels reproduce compiled generator types", "[item_stats]") {
    REQUIRE(item_equip_type_from_name("Weapon") == ItemEquipType::Weapon);
    REQUIRE(item_equip_type_from_name("weapon1or2") == ItemEquipType::Weapon);
    REQUIRE(item_equip_type_from_name("WEAPON2") == ItemEquipType::TwoHandedWeapon);
    REQUIRE(item_equip_type_from_name("Missile") == ItemEquipType::Missile);
    REQUIRE(item_equip_type_from_name("Armor") == ItemEquipType::Armor);
    REQUIRE(item_equip_type_from_name("Amulet") == ItemEquipType::Amulet);
    REQUIRE(item_equip_type_from_name("WeaponW") == ItemEquipType::Wand);
    REQUIRE(item_equip_type_from_name("Gold") == ItemEquipType::Gold);
    REQUIRE(item_equip_type_from_name("unknown") == ItemEquipType::Other);
    REQUIRE(item_equip_type_name(ItemEquipType::Gauntlets) == "gauntlets");
    REQUIRE(item_skill_type_from_name("Club") == ItemSkillType::Club);
    REQUIRE(item_skill_type_from_name("SWORD") == ItemSkillType::Sword);
    REQUIRE(item_skill_type_from_name("Leather") == ItemSkillType::Leather);
    REQUIRE(item_skill_type_from_name("unknown") == ItemSkillType::Misc);
    REQUIRE(item_skill_type_name(ItemSkillType::Plate) == "plate");
}

TEST_CASE("ITEMS rejects a missing header or non-contiguous ids", "[item_stats]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("something\telse\r\n0\tblank\r\n", table) ==
            TextTableError::None);
    ItemStatsTable items;
    REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::NoHeader);

    REQUIRE(TextTable::parse_body("Item #\tPic File\r\n0\tblank\r\n2\tsword01\r\n", table) ==
            TextTableError::None);
    REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::BadId);
    REQUIRE(items.size() == 0);
}

namespace {

std::string random_items_body() {
    return "Random items\r\n"
           "Item #\tPic File\t1\t2\t3\t4\t5\t6\r\n"
           "0\tblank\t0\t0\t0\t0\t0\t0\r\n"
           "1\tblade1\t5\t10\t2\t0\t0\t0\r\n"
           "2\tblade2\t2\t0\t0\t0\t0\t0\r\n"
           "\r\n"
           "Bonus chance by level %\t\t1\t2\t3\t4\t5\t6\r\n"
           "\tStandard\t0\t40\t40\t40\t40\t75\r\n"
           "\tSpecial\t0\t0\t10\t15\t20\t25\r\n"
           "Weapons\tSpecial %\t0\t0\t10\t20\t30\t50\r\n";
}

std::string standard_bonuses_body() {
    std::string s;
    s += "Standard bonuses\r\n";
    s += "Bonus Stat\tOf Name\tArm\tShld\tHelm\tBelt\tCape\tGaunt\tBoot\tRing\tAmul\r\n";
    s += "\r\n";
    s += "Might\tof Might\t5\t0\t10\t10\t10\t10\t5\t10\t10\r\n";
    s += "Luck\tof Luck\t5\t0\t10\t10\t10\t10\t10\t10\t10\r\n";
    s += "\r\n";
    s += "\tBonus range\r\n";
    s += "\tlvl\tmin\tmax\r\n";
    s += "\t1\t0\t0\r\n";
    s += "\t2\t1\t5\r\n";
    s += "\t3\t3\t8\r\n";
    s += "\t4\t6\t12\r\n";
    s += "\t5\t10\t17\r\n";
    s += "\t6\t15\t25\r\n";
    return s;
}

std::string special_bonuses_body() {
    std::string s;
    s += "Special bonuses\r\n";
    s += "Bonus Stat\tName Add\tW1\tW2\tMiss\tArm\tShld\tHelm\tBelt\tCape\tGaunt\tBoot"
         "\tRing\tAmul\tValue\tLvl\tDescription\r\n";
    s += "\r\n";
    s += "Protects the wearer\tof Protection\t0\t0\t0\t10\t10\t10\t0\t10\t0\t0"
         "\t10\t10\t1000\tB\tfixture protection\r\n";
    s += "Drains a target\tVampiric\t5\t5\t5\t0\t0\t0\t0\t0\t0\t0\t0\t0"
         "\tX 2\tD\tfixture drain\r\n";
    s += "\r\n";
    s += "footer\tmust not parse\r\n";
    return s;
}

std::string generation_items_body(std::string_view equip_stat, int modifier_2 = 0,
                                  int id_rep_st = 0, std::string_view skill_group = "Misc") {
    std::string s;
    s += "Items\r\n";
    s += "Item #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
         "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    s += "0\tblank\t\t0\t0\t0\t0\t0\t0\t0\t\t0\t0\t0\t0\tplaceholder\r\n";
    s += "1\tfixture\tFixture\t10\t";
    s += equip_stat;
    s += "\t";
    s += skill_group;
    s += "\t0\t";
    s += std::to_string(modifier_2);
    s += "\t1\t";
    s += std::to_string(id_rep_st);
    s += "\tFixture\t1\t0\t0\t0\tfixture\r\n";
    return s;
}

std::string generation_random_items_body() {
    return "Random items\r\n"
           "Item #\tPic File\t1\t2\t3\t4\t5\t6\r\n"
           "0\tblank\t0\t0\t0\t0\t0\t0\r\n"
           "1\tfixture\t10\t10\t10\t10\t10\t10\r\n"
           "\r\n"
           "Bonus chance by level %\t\t1\t2\t3\t4\t5\t6\r\n"
           "\tStandard\t0\t40\t40\t40\t40\t75\r\n"
           "\tSpecial\t0\t0\t10\t15\t20\t25\r\n"
           "Weapons\tSpecial %\t0\t0\t10\t20\t30\t50\r\n";
}

void load_generation_tables(std::string_view equip_stat, int modifier_2, int id_rep_st,
                            ItemStatsTable& items, RandomItemTable& random_items,
                            StandardBonusTable& standard_bonuses,
                            SpecialBonusTable& special_bonuses,
                            std::string_view skill_group = "Misc") {
    TextTable table;
    REQUIRE(
        TextTable::parse_body(generation_items_body(equip_stat, modifier_2, id_rep_st, skill_group),
                              table) == TextTableError::None);
    REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::None);
    REQUIRE(TextTable::parse_body(generation_random_items_body(), table) == TextTableError::None);
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::None);
    REQUIRE(TextTable::parse_body(standard_bonuses_body(), table) == TextTableError::None);
    REQUIRE(StandardBonusTable::parse(table, standard_bonuses) == StandardBonusError::None);
    REQUIRE(TextTable::parse_body(special_bonuses_body(), table) == TextTableError::None);
    REQUIRE(SpecialBonusTable::parse(table, special_bonuses) == SpecialBonusError::None);
}

}  // namespace

TEST_CASE("RNDITEMS rows keep direct ids and six treasure weights", "[item_generation]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(random_items_body(), table) == TextTableError::None);
    RandomItemTable random_items;
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::None);
    REQUIRE(random_items.size() == 3);
    REQUIRE(random_items.at(0)->picture == "blank");
    REQUIRE(random_items.at(1)->picture == "blade1");
    REQUIRE(random_items.at(1)->weights == std::array<int, 6>{5, 10, 2, 0, 0, 0});
    REQUIRE(random_items.at(2)->picture == "blade2");
    REQUIRE(random_items.at(3) == nullptr);

    const auto& chances = random_items.bonus_chances();
    REQUIRE(chances.standard == std::array<int, 6>{0, 40, 40, 40, 40, 75});
    REQUIRE(chances.special == std::array<int, 6>{0, 0, 10, 15, 20, 25});
    REQUIRE(chances.weapon_special == std::array<int, 6>{0, 0, 10, 20, 30, 50});
    REQUIRE(random_items.total_weight(0) == 0);
    REQUIRE(random_items.total_weight(1) == 7);
    REQUIRE(random_items.select_for_roll(1, 0)->id == 1);
    REQUIRE(random_items.select_for_roll(1, 5)->id == 1);
    REQUIRE(random_items.select_for_roll(1, 6)->id == 2);
    REQUIRE(random_items.select_for_roll(1, 7) == nullptr);
}

TEST_CASE("RNDITEMS rejects missing headers and non-contiguous ids", "[item_generation]") {
    TextTable table;
    REQUIRE(TextTable::parse_body("other\theader\r\n", table) == TextTableError::None);
    RandomItemTable random_items;
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::NoHeader);

    REQUIRE(TextTable::parse_body("Item #\tPic File\r\n0\tblank\r\n2\tblade\r\n", table) ==
            TextTableError::None);
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::BadId);

    REQUIRE(TextTable::parse_body("Item #\tPic File\r\n0\tblank\r\n", table) ==
            TextTableError::None);
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::NoBonusChanceHeader);
}

TEST_CASE("RNDITEMS rejects malformed bonus chances", "[item_generation]") {
    TextTable table;
    std::string body = random_items_body();
    body[body.find("\tSpecial\t0\t0\t10") + 13] = '7';
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    RandomItemTable random_items;
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::BadBonusChances);
}

TEST_CASE("item bonus chances reproduce weapon and equipment branches", "[item_generation]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(random_items_body(), table) == TextTableError::None);
    RandomItemTable random_items;
    REQUIRE(RandomItemTable::parse(table, random_items) == RandomItemError::None);
    const auto& chances = random_items.bonus_chances();

    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 3, 39) ==
            ItemBonusKind::Standard);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 3, 40) ==
            ItemBonusKind::Special);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 3, 49) ==
            ItemBonusKind::Special);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 3, 50) == ItemBonusKind::None);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Weapon, 6, 49) == ItemBonusKind::Special);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Weapon, 6, 50) == ItemBonusKind::None);
    REQUIRE(classify_item_bonus(chances, ItemBonusTarget::Other, 6, 0) == ItemBonusKind::None);
    REQUIRE_FALSE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 0, 0));
    REQUIRE_FALSE(classify_item_bonus(chances, ItemBonusTarget::Equipment, 1, 100));
}

TEST_CASE("STDITEMS selectors and strength ranges are one-based", "[item_generation]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(standard_bonuses_body(), table) == TextTableError::None);
    StandardBonusTable bonuses;
    REQUIRE(StandardBonusTable::parse(table, bonuses) == StandardBonusError::None);
    REQUIRE(bonuses.size() == 2);
    REQUIRE(bonuses.at(0) == nullptr);
    REQUIRE(bonuses.at(1)->stat == "Might");
    REQUIRE(bonuses.at(1)->name_suffix == "of Might");
    REQUIRE(bonuses.at(1)->chance_by_item_type[0] == 5);
    REQUIRE(bonuses.at(2)->stat == "Luck");
    REQUIRE(bonuses.at(3) == nullptr);
    REQUIRE(bonuses.range(0) == nullptr);
    REQUIRE(bonuses.range(2)->minimum == 1);
    REQUIRE(bonuses.range(2)->maximum == 5);
    REQUIRE(bonuses.range(6)->minimum == 15);
    REQUIRE(bonuses.range(7) == nullptr);
    REQUIRE(bonuses.total_weight(0) == 10);
    REQUIRE(bonuses.total_weight(1) == 0);
    REQUIRE(bonuses.select_for_roll(0, 0)->id == 1);
    REQUIRE(bonuses.select_for_roll(0, 5)->id == 1);
    REQUIRE(bonuses.select_for_roll(0, 6)->id == 2);
    REQUIRE(bonuses.select_for_roll(0, 10) == nullptr);
    REQUIRE(bonuses.select_for_roll(1, 0) == nullptr);
}

TEST_CASE("STDITEMS rejects incomplete strength ranges", "[item_generation]") {
    TextTable table;
    std::string body = standard_bonuses_body();
    body.erase(body.find("\t6\t15\t25\r\n"));
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    StandardBonusTable bonuses;
    REQUIRE(StandardBonusTable::parse(table, bonuses) == StandardBonusError::BadLevel);
}

TEST_CASE("SPCITEMS selectors stop before footer rows", "[item_generation]") {
    TextTable table;
    REQUIRE(TextTable::parse_body(special_bonuses_body(), table) == TextTableError::None);
    SpecialBonusTable bonuses;
    REQUIRE(SpecialBonusTable::parse(table, bonuses) == SpecialBonusError::None);
    REQUIRE(bonuses.size() == 2);
    REQUIRE(bonuses.at(0) == nullptr);
    REQUIRE(bonuses.at(1)->name_affix == "of Protection");
    REQUIRE(bonuses.at(1)->chance_by_item_type[3] == 10);
    REQUIRE(bonuses.at(1)->value == "1000");
    REQUIRE(bonuses.at(1)->treasure_class == SpecialBonusTreasureClass::B);
    REQUIRE(bonuses.at(2)->name_affix == "Vampiric");
    REQUIRE(bonuses.at(2)->value == "X 2");
    REQUIRE(bonuses.at(2)->treasure_class == SpecialBonusTreasureClass::D);
    REQUIRE(bonuses.at(3) == nullptr);
    REQUIRE(bonuses.eligible(*bonuses.at(1), 3));
    REQUIRE_FALSE(bonuses.eligible(*bonuses.at(2), 3));
    REQUIRE_FALSE(bonuses.eligible(*bonuses.at(1), 6));
    REQUIRE(bonuses.eligible(*bonuses.at(2), 6));
    REQUIRE(bonuses.total_weight(3, 3) == 10);
    REQUIRE(bonuses.select_for_roll(3, 3, 0)->id == 1);
    REQUIRE(bonuses.total_weight(0, 6) == 5);
    REQUIRE(bonuses.select_for_roll(0, 6, 4)->id == 2);
    REQUIRE(bonuses.select_for_roll(0, 6, 5) == nullptr);
}

TEST_CASE("SPCITEMS rejects unknown treasure classes", "[item_generation]") {
    TextTable table;
    std::string body = special_bonuses_body();
    body.replace(body.find("\tB\tfixture"), 3, "\tZ\t");
    REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
    SpecialBonusTable bonuses;
    REQUIRE(SpecialBonusTable::parse(table, bonuses) == SpecialBonusError::BadTreasureClass);
}

TEST_CASE("item generation preserves equipment random-call order", "[item_generation]") {
    ItemStatsTable items;
    RandomItemTable random_items;
    StandardBonusTable standard_bonuses;
    SpecialBonusTable special_bonuses;
    load_generation_tables("Armor", 0, 0, items, random_items, standard_bonuses, special_bonuses);

    ArtifactGenerationState artifacts;
    Mm6Random random(1);
    GeneratedItem item;
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 3, random,
                                 artifacts, item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(item.standard_bonus == 1);
    REQUIRE(item.standard_bonus_strength == 8);
    REQUIRE(item.special_bonus == 0);
    REQUIRE(item.identified);
    REQUIRE(random.state() == UINT32_C(0xCAE1DF84));

    Mm6Random special_random(38);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 3,
                                 special_random, artifacts, item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(item.standard_bonus == 0);
    REQUIRE(item.special_bonus == 1);
    REQUIRE(special_random.state() == UINT32_C(0xFD6B0CCA));
}

TEST_CASE("item generation preserves weapon and wand random-call order", "[item_generation]") {
    ItemStatsTable items;
    RandomItemTable random_items;
    StandardBonusTable standard_bonuses;
    SpecialBonusTable special_bonuses;
    ArtifactGenerationState artifacts;
    GeneratedItem item;

    load_generation_tables("Weapon", 0, 3, items, random_items, standard_bonuses, special_bonuses);
    Mm6Random weapon_random(1);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 6,
                                 weapon_random, artifacts, item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(item.special_bonus == 2);
    REQUIRE_FALSE(item.identified);
    REQUIRE(weapon_random.state() == UINT32_C(0xCAE1DF84));

    load_generation_tables("WeaponW", 7, 0, items, random_items, standard_bonuses, special_bonuses);
    Mm6Random wand_random(1);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 3,
                                 wand_random, artifacts, item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(item.charges == 11);
    REQUIRE(wand_random.state() == UINT32_C(0x18BE873A));
}

TEST_CASE("level-six item generation tracks the thirteen-artifact cap", "[item_generation]") {
    ItemStatsTable items;
    RandomItemTable random_items;
    StandardBonusTable standard_bonuses;
    SpecialBonusTable special_bonuses;
    load_generation_tables("Armor", 0, 0, items, random_items, standard_bonuses, special_bonuses);

    ArtifactGenerationState artifacts;
    Mm6Random random(17);
    GeneratedItem item;
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 6, random,
                                 artifacts, item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 404);
    REQUIRE(artifacts.found[4]);
    REQUIRE_FALSE(item.identified);
    REQUIRE(random.state() == UINT32_C(0x67EA7713));

    load_generation_tables("Herb", 0, 0, items, random_items, standard_bonuses, special_bonuses);
    ArtifactGenerationState capped_artifacts;
    for (std::size_t index = 5; index < 18; ++index) {
        capped_artifacts.found[index] = true;
    }
    Mm6Random capped_random(17);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 6,
                                 capped_random, capped_artifacts,
                                 item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE_FALSE(capped_artifacts.found[4]);
    REQUIRE(capped_random.state() == UINT32_C(0x7541458A));
}

TEST_CASE("restricted item generation filters compiled skill and equipment types",
          "[item_generation]") {
    ItemStatsTable items;
    RandomItemTable random_items;
    StandardBonusTable standard_bonuses;
    SpecialBonusTable special_bonuses;
    ArtifactGenerationState artifacts;
    GeneratedItem item;

    load_generation_tables("Weapon", 0, 0, items, random_items, standard_bonuses, special_bonuses,
                           "Sword");
    Mm6Random sword_random(1);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 1,
                                 ItemGenerationType::Sword, sword_random, artifacts,
                                 item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(sword_random.state() == UINT32_C(0x0029E2C0));

    load_generation_tables("Ring", 0, 0, items, random_items, standard_bonuses, special_bonuses);
    Mm6Random ring_random(1);
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 1,
                                 ItemGenerationType::RingCategory, ring_random, artifacts,
                                 item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 1);
    REQUIRE(ring_random.state() == UINT32_C(0x0029E2C0));
}

TEST_CASE("an empty restricted category consumes no selector call", "[item_generation]") {
    ItemStatsTable items;
    RandomItemTable random_items;
    StandardBonusTable standard_bonuses;
    SpecialBonusTable special_bonuses;
    load_generation_tables("Ring", 0, 0, items, random_items, standard_bonuses, special_bonuses);

    ArtifactGenerationState artifacts;
    Mm6Random random(1);
    GeneratedItem item;
    REQUIRE(generate_random_item(random_items, items, standard_bonuses, special_bonuses, 1,
                                 ItemGenerationType::TwoHandedWeapon, random, artifacts,
                                 item) == ItemGenerationError::None);
    REQUIRE(item.item_id == 0);
    REQUIRE(item.identified);
    REQUIRE(random.state() == 1);
}

TEST_CASE("chest placeholder and map classes resolve treasure-level ranges", "[item_generation]") {
    REQUIRE(chest_treasure_level_range(1, 0) == ChestTreasureLevelRange{1, 1});
    REQUIRE(chest_treasure_level_range(2, 1) == ChestTreasureLevelRange{1, 2});
    REQUIRE(chest_treasure_level_range(4, 3) == ChestTreasureLevelRange{3, 4});
    REQUIRE(chest_treasure_level_range(6, 5) == ChestTreasureLevelRange{5, 6});
    REQUIRE(chest_treasure_level_range(6, 6) == ChestTreasureLevelRange{6, 6});
    REQUIRE_FALSE(chest_treasure_level_range(0, 0).has_value());
    REQUIRE_FALSE(chest_treasure_level_range(7, 0).has_value());
    REQUIRE_FALSE(chest_treasure_level_range(1, 7).has_value());

    Mm6Random random(1);
    REQUIRE(roll_chest_treasure_level(6, 5, random) == 6);
    REQUIRE(random.state() == UINT32_C(0x0029E2C0));

    Mm6Random invalid_random(1);
    REQUIRE_FALSE(roll_chest_treasure_level(0, 0, invalid_random).has_value());
    REQUIRE(invalid_random.state() == 1);
}
