// Tests for hitting things and being hit.
//
// Hermetic: the monster and item rows and the session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/combat.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

// One MONSTERS.TXT row, with the combat columns under test.
data::MonsterStatsTable monsters(const char* hp, const char* ac, const char* damage,
                                 const char* cold = "0", const char* treasure = "0") {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += std::string("1\tRatA\tCommon Rat\t2\t") + hp + "\t" + ac + "\t24\t" + treasure +
            "\t0\tN\tMed\tAggress\t4\t200\t100\t0\t0\tCold\t" + damage +
            "\t0\t100\t0\t0\t0\t0\t0\t0\t0\t" + cold + "\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

data::ItemStatsTable items() {
    std::string body =
        "\r\nItem #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
        "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    body += "0\t\t\t0\t\t\t0\t0\t0\t0\t\t0\t0\t0\t0\t\r\n";
    body += "1\tlsword1\tLongsword\t50\tWeapon\tSword\t3d3\t0\t8\t1\tLongsword\t1\t4\t0\t0\t\r\n";
    body += "2\tring1\tRing\t100\tRing\t\t0\t0\t8\t1\tRing\t2\t4\t0\t0\t\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::ItemStatsTable out;
    REQUIRE(data::ItemStatsTable::parse(table, out) == data::ItemStatsError::None);
    return out;
}

world::MapSession with_monster(render::Vec3 at) {
    world::MapSession s;
    s.kind = world::MapKind::Indoor;
    s.actors.push_back({"ratA", "Common Rat", 1, at});
    return s;
}

Character fighter() {
    Character c;
    c.name = "Aaron";
    c.hit_points = 30;
    c.max_hit_points = 30;
    c.attributes.fill(15);
    return c;
}

}  // namespace

TEST_CASE("a monster starts at the hit points its row gives", "[combat]") {
    const auto session = with_monster({0, 0, 0});
    Battle battle;
    battle.reset(session, monsters("6", "4", "1d4"), 1);
    REQUIRE(battle.size() == 1);
    REQUIRE(battle.alive(0));
    REQUIRE_FALSE(battle.alive(99));
}

TEST_CASE("enough blows kill", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    std::string last;
    for (int i = 0; i < 40 && battle.alive(0); ++i) {
        last = battle.strike(0, who, pack, session, table, items(), {}, {}, {});
        REQUIRE_FALSE(last.empty());
    }
    REQUIRE_FALSE(battle.alive(0));
    REQUIRE(last.find("kills it") != std::string::npos);
}

TEST_CASE("something already dead cannot be struck again", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("1", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 5);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    REQUIRE(battle.strike(0, who, pack, session, table, items(), {}, {}, {}).empty());
}

TEST_CASE("a character swings what is in their weapon slot", "[combat]") {
    // A longsword is 3d3 and a fist is 1d3. Before there were slots this took
    // the first weapon in the pack, so picking something up changed what you
    // were fighting with.
    const auto table = items();
    Character bare = fighter();
    REQUIRE(weapon_of(bare, table).sides == kBareHandSides);
    REQUIRE(weapon_of(bare, table).count == 1);

    Character armed = fighter();
    armed.equipped[static_cast<std::size_t>(Slot::Weapon)] = 1;
    REQUIRE(weapon_of(armed, table).count == 3);
    REQUIRE(weapon_of(armed, table).sides == 3);

    // A ring in the weapon slot is not a weapon, so a fist it is.
    Character wrong = fighter();
    wrong.equipped[static_cast<std::size_t>(Slot::Weapon)] = 2;
    REQUIRE(weapon_of(wrong, table).count == 1);
}

TEST_CASE("an item goes in the slot its equip type names", "[combat]") {
    REQUIRE(slot_for(data::ItemEquipType::Weapon) == Slot::Weapon);
    REQUIRE(slot_for(data::ItemEquipType::TwoHandedWeapon) == Slot::Weapon);
    REQUIRE(slot_for(data::ItemEquipType::Missile) == Slot::Weapon);
    REQUIRE(slot_for(data::ItemEquipType::Ring) == Slot::Ring);
    REQUIRE(slot_for(data::ItemEquipType::Armor) == Slot::Armor);
    // A bottle is carried, not worn.
    REQUIRE(slot_for(data::ItemEquipType::Bottle) == Slot::Count);
    REQUIRE(slot_for(data::ItemEquipType::Gold) == Slot::Count);
    REQUIRE_FALSE(slot_name(Slot::Weapon).empty());
}

TEST_CASE("a monster in reach hits somebody standing", "[combat]") {
    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    bool swung = false;
    int hurt = 0;
    // Half its blows miss, so this waits for one to land rather than for one
    // to be thrown.
    for (int i = 0; i < 200 && hurt == 0; ++i) {
        // Not `swung = swung || update(...)`: that stops swinging as soon as
        // the first blow is thrown, and the first one is usually a miss.
        const std::string blow = battle.update(0.5f, session, table, party, {0, 0, 0});
        swung = swung || !blow.empty();
        hurt = 0;
        for (const auto& who : party) {
            hurt += who.hit_points < who.max_hit_points ? 1 : 0;
        }
    }
    REQUIRE(swung);
    REQUIRE(hurt >= 1);
}

TEST_CASE("a monster out of reach hits nobody", "[combat]") {
    auto session = with_monster({0, 0, kMeleeRange * 4});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (int i = 0; i < 100; ++i) {
        REQUIRE(battle.update(0.5f, session, table, party, {0, 0, 0}).empty());
    }
    REQUIRE(party[0].hit_points == party[0].max_hit_points);
}

TEST_CASE("immunity stops a blow entirely", "[combat]") {
    REQUIRE(after_resistance(10, data::kResistanceImmune) == 0);
    REQUIRE(after_resistance(10, 0) == 10);
    REQUIRE(after_resistance(10, 50) == 5);
    // A resisted blow still stings: rounding must not reach zero by itself.
    REQUIRE(after_resistance(1, 90) == 1);
}

TEST_CASE("an attack is answered by the resistance of its own type", "[combat]") {
    const auto table = monsters("6", "0", "1d4", "Imm");
    const auto& monster = table.entries()[0];
    REQUIRE(resistance_to(monster, "Cold") == data::kResistanceImmune);
    REQUIRE(resistance_to(monster, "Fire") == 0);
    // Physical damage has no resistance column of its own; armour answers it.
    REQUIRE(resistance_to(monster, "Phys") == 0);
    REQUIRE(resistance_to(monster, "Ener") == 0);
}

TEST_CASE("the party aims at what it is looking at", "[combat]") {
    world::MapSession session;
    session.kind = world::MapKind::Indoor;
    session.actors.push_back({"ratA", "Common Rat", 1, {0, 0, -200}});
    session.actors.push_back({"ratA", "Common Rat", 1, {200, 0, 0}});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 1);

    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, -1}, kPartyReach) == 0);
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {1, 0, 0}, kPartyReach) == 1);
    // Nothing behind you, and nothing beyond arm's length.
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, 1}, kPartyReach) == kNoActor);
    REQUIRE(aimed_actor(session, battle, {0, 0, 0}, {0, 0, -1}, 100.0f) == kNoActor);
}

TEST_CASE("a monster flinches when hit and lies there when killed", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Stand);

    Character who = fighter();
    Pack pack;
    // Land one blow, whatever it takes.
    while (battle.alive(0) && battle.animation_of(0) == world::MonsterAnimation::Stand) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    if (battle.alive(0)) {
        REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Wince);
    }
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Death);

    // A corpse stays a corpse: no amount of time stands it back up.
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (int i = 0; i < 20; ++i) {
        (void)battle.update(1.0f, session, table, party, {0, 0, 0});
    }
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Death);
}

TEST_CASE("a flinch passes", "[combat]") {
    auto session = with_monster({0, 0, kMeleeRange * 4});
    const auto table = monsters("100", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.animation_of(0) == world::MonsterAnimation::Stand) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    (void)battle.update(kWinceSeconds * 2.0f, session, table, party, {0, 0, 0});
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Stand);
}

TEST_CASE("a kill is worth what the monster's row says", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);
    REQUIRE(battle.unclaimed_experience() == 0);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    // The fixture's row gives 24.
    REQUIRE(battle.unclaimed_experience() == 24);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    battle.award(party);
    REQUIRE(battle.unclaimed_experience() == 0);
    for (const auto& member : party) {
        REQUIRE(member.experience == 6);
    }
}

TEST_CASE("only the standing collect", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    party[0].hit_points = 0;
    party[1].hit_points = 0;
    battle.award(party);
    REQUIRE(party[0].experience == 0);
    REQUIRE(party[2].experience == 12);
}

TEST_CASE("a party with nobody standing keeps what it has earned", "[combat]") {
    // Otherwise a kill that drops the last character loses its experience.
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (auto& member : party) {
        member.hit_points = 0;
    }
    battle.award(party);
    REQUIRE(battle.unclaimed_experience() == 24);
}

TEST_CASE("someone who is down cannot swing", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("20", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    who.hit_points = 0;
    Pack pack;
    REQUIRE(battle.strike(0, who, pack, session, table, items(), {}, {}, {}).empty());
    REQUIRE(battle.alive(0));
}

TEST_CASE("a refill puts the fallen back on their feet", "[combat]") {
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Death);

    battle.refill();
    REQUIRE(battle.alive(0));
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Stand);
}

TEST_CASE("what is near is what is alive and near", "[combat]") {
    auto session = with_monster({0, 0, 300});
    const auto table = monsters("6", "0", "1d4");
    Battle battle;
    battle.reset(session, table, 3);

    REQUIRE(battle.anything_near(session, {0, 0, 0}, 1000.0f));
    REQUIRE_FALSE(battle.anything_near(session, {0, 0, 0}, 100.0f));

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    // A corpse is not company.
    REQUIRE_FALSE(battle.anything_near(session, {0, 0, 0}, 1000.0f));
}

namespace {

// The random-item weights, pointing at the two items above. Level 1 carries
// no enchantment chances, so what a kill leaves is exact and the two bonus
// tables can stay empty.
data::RandomItemTable random_items() {
    const std::string body =
        "Item #\tPic File\t1\t2\t3\t4\t5\t6\r\n"
        "0\tblank\t0\t0\t0\t0\t0\t0\r\n"
        "1\tlsword1\t5\t0\t0\t0\t0\t0\r\n"
        "2\tring1\t5\t0\t0\t0\t0\t0\r\n"
        "\r\n"
        "Bonus chance by level %\t\t1\t2\t3\t4\t5\t6\r\n"
        "\tStandard\t0\t40\t40\t40\t40\t75\r\n"
        "\tSpecial\t0\t0\t10\t15\t20\t25\r\n"
        "Weapons\tSpecial %\t0\t0\t10\t20\t30\t50\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::RandomItemTable out;
    REQUIRE(data::RandomItemTable::parse(table, out) == data::RandomItemError::None);
    return out;
}

}  // namespace

TEST_CASE("a treasure code's kind is a kind the generator takes", "[combat]") {
    REQUIRE(data::treasure_item_type("Bow") == data::ItemGenerationType::Bow);
    REQUIRE(data::treasure_item_type("ring") == data::ItemGenerationType::Ring);
    REQUIRE(data::treasure_item_type("Misc") == data::ItemGenerationType::Misc);
    // An unnamed kind is any kind, and so is one the generator has no word for.
    REQUIRE(data::treasure_item_type("") == data::ItemGenerationType::Any);
    REQUIRE(data::treasure_item_type("nonsense") == data::ItemGenerationType::Any);
}

TEST_CASE("a kill pays out its treasure code", "[combat]") {
    auto session = with_monster({0, 0, 0});
    // Ten one-sided dice of gold and a level-1 ring: exact, and no chance
    // prefix means it always drops.
    const auto table = monsters("6", "0", "1d4", "0", "10D1+L1Ring");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), random_items(), {}, {});
    }
    REQUIRE(battle.unclaimed_gold() == 10);
    // The kind lets only the ring, id 2, through.
    REQUIRE(battle.unclaimed_loot() == std::vector<int>{2});

    REQUIRE(battle.take_gold() == 10);
    REQUIRE(battle.take_gold() == 0);
    REQUIRE(battle.take_loot() == std::vector<int>{2});
    REQUIRE(battle.unclaimed_loot().empty());
}

TEST_CASE("a kind nothing matches drops nothing", "[combat]") {
    auto session = with_monster({0, 0, 0});
    // No item in the table is a bow, and the blank row must not stand in.
    const auto table = monsters("6", "0", "1d4", "0", "L1Bow");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), random_items(), {}, {});
    }
    REQUIRE(battle.unclaimed_loot().empty());
}

TEST_CASE("claiming experience does not claim the gold", "[combat]") {
    // award() used to zero the gold with the experience, and the shell takes
    // the gold after awarding: the frame of the kill lost its payout.
    auto session = with_monster({0, 0, 0});
    const auto table = monsters("6", "0", "1d4", "0", "10D1");
    Battle battle;
    battle.reset(session, table, 3);

    Character who = fighter();
    Pack pack;
    while (battle.alive(0)) {
        (void)battle.strike(0, who, pack, session, table, items(), {}, {}, {});
    }
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    battle.award(party);
    REQUIRE(battle.take_gold() == 10);
}

TEST_CASE("armour is the flat modifier of what is worn", "[combat]") {
    // A weapon's modifier is dice and must not be counted as armour; armour's
    // is a plain number.
    std::string body =
        "\r\nItem #\tPic File\tName\tValue\tEquip Stat\tSkill Group\tMod1\tMod2\tmaterial"
        "\tID/Rep/St\tNot identified name\tSprite Index\tShape\tEquip X\tEquip Y\tNotes\r\n";
    body += "0\t\t\t0\t\t\t0\t0\t0\t0\t\t0\t0\t0\t0\t\r\n";
    body += "1\tlsword1\tLongsword\t50\tWeapon\tSword\t3d3\t0\t8\t1\tLongsword\t1\t4\t0\t0\t\r\n";
    body += "2\tleather\tLeather\t30\tArmor\tLeather\t4\t0\t8\t1\tLeather\t2\t4\t0\t0\t\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::ItemStatsTable armoury;
    REQUIRE(data::ItemStatsTable::parse(table, armoury) == data::ItemStatsError::None);

    Character who = fighter();
    REQUIRE(armour_of(who, armoury) == 0);
    who.equipped[static_cast<std::size_t>(Slot::Armor)] = 2;
    REQUIRE(armour_of(who, armoury) == 4);
    // The sword adds nothing: its modifier is 3d3, not a number.
    who.equipped[static_cast<std::size_t>(Slot::Weapon)] = 1;
    REQUIRE(armour_of(who, armoury) == 4);
}
