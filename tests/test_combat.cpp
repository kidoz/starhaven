// Tests for hitting things and being hit.
//
// Hermetic: the monster and item rows and the session are built by hand.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "game/combat.hpp"

using namespace starhaven;
using namespace starhaven::game;

namespace {

// One MONSTERS.TXT row, with the combat columns under test.
data::MonsterStatsTable monsters(const char* hp, const char* ac, const char* damage,
                                 const char* cold = "0", const char* treasure = "0",
                                 const char* use_percent = "0", const char* spells = "0") {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += std::string("1\tRatA\tCommon Rat\t2\t") + hp + "\t" + ac + "\t24\t" + treasure +
            "\t0\tN\tMed\tAggress\t4\t200\t100\t0\t0\tCold\t" + damage +
            "\t0\t100\t0\t0\t0\t" + use_percent + "\t" + spells + "\t0\t0\t" + cold +
            "\t0\t0\t0\t0\r\n";
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

TEST_CASE("the Rec column spends at sixty points a second", "[combat]") {
    // Traced: the counter is set from a queued amount times 32/15 and drained
    // by the world clock's own units, of which a real second holds 128.
    REQUIRE(game::recovery_seconds(60) == Catch::Approx(1.0f));
    REQUIRE(game::recovery_seconds(0) == 0.0f);
    // The slowest rows in MONSTERS.TXT sit near 200, so under four seconds.
    REQUIRE(game::recovery_seconds(200) < 4.0f);
    // 32/15 units a point against 128 units a second is the whole of it.
    REQUIRE(game::kClockUnitsPerSecond / (32.0f / 15.0f) == Catch::Approx(60.0f));
}

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
        const std::string blow = battle.update(0.5f, session, table, {}, party, {0, 0, 0});
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
        REQUIRE(battle.update(0.5f, session, table, {}, party, {0, 0, 0}).empty());
    }
    REQUIRE(party[0].hit_points == party[0].max_hit_points);
}

TEST_CASE("a blow lands by the original's own roll", "[combat]") {
    // Traced to 0x421cb0: roll `rand() % (armour + 2*attack + 30)` and
    // reach the kind's bar — the plain armour+15, a shot 2*armour+30.
    Mm6Random random{11};
    const auto rate = [&](int armour, int attack, BlowKind kind) {
        int landed = 0;
        for (int i = 0; i < 2000; ++i) {
            landed += blow_lands(armour, attack, kind, 0, random) ? 1 : 0;
        }
        return landed;
    };
    // Unarmoured, unskilled: the span is 30 and the bar 15 — about half.
    const int even = rate(0, 0, BlowKind::Plain);
    REQUIRE(even > 800);
    REQUIRE(even < 1200);
    // Attack bonus widens the span without moving the melee bar.
    REQUIRE(rate(0, 20, BlowKind::Plain) > even);
    // A shot's bar climbs with armour twice as fast as the span, so an
    // unskilled archer cannot touch an armoured target at all, while a
    // skilled one still can.
    REQUIRE(rate(60, 0, BlowKind::Shot) == 0);
    REQUIRE(rate(60, 60, BlowKind::Shot) > 0);
}

TEST_CASE("a monster's missile flies on the shot's own bar", "[combat]") {
    // A shot's bar is 2*armour+30 against a span of armour+2*level+30, so
    // a low-level archer cannot reach a well-armoured party at all while
    // its melee neighbour still can.
    const auto shooters = monsters("3", "0", "1d4", "0", "0", "0", "0");
    Mm6Random random{5};
    const auto& row = shooters.entries()[0];
    int plain = 0, shot = 0;
    for (int i = 0; i < 500; ++i) {
        plain += blow_lands(50, row.level, BlowKind::Plain, 0, random) ? 1 : 0;
        shot += blow_lands(50, row.level, BlowKind::Shot, 0, random) ? 1 : 0;
    }
    REQUIRE(plain > 0);
    REQUIRE(shot == 0);
}

TEST_CASE("resistance halves by the original's own rolls", "[combat]") {
    // Traced to the routine at 0x421dc0: immunity stops a blow, a zero
    // resistance passes it whole, and any real resistance buys repeated
    // halvings — up to four, each on a `rand() % (resistance + 30)` that
    // lands 30 or above. The dice-free overload returns the expectation,
    // so it falls monotonically with the resistance.
    REQUIRE(after_resistance(10, data::kResistanceImmune) == 0);
    REQUIRE(after_resistance(10, 0) == 10);
    REQUIRE(after_resistance(100, 50) < 100);
    REQUIRE(after_resistance(100, 100) < after_resistance(100, 50));

    // And with dice in hand: a high resistance halves four times at most,
    // never below a sixteenth, and 200 or more is immune outright.
    Mm6Random random{7};
    for (int i = 0; i < 50; ++i) {
        const int through = after_resistance(160, 150, random);
        REQUIRE(through >= 10);
        REQUIRE(through <= 160);
    }
    REQUIRE(after_resistance(10, data::kResistanceImmune, random) == 0);
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
        (void)battle.update(1.0f, session, table, {}, party, {0, 0, 0});
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
    (void)battle.update(kWinceSeconds * 2.0f, session, table, {}, party, {0, 0, 0});
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
    REQUIRE(battle.unclaimed_loot().size() == 1);
    REQUIRE(battle.unclaimed_loot()[0].item_id == 2);

    REQUIRE(battle.take_gold() == 10);
    REQUIRE(battle.take_gold() == 0);
    const auto taken = battle.take_loot();
    REQUIRE(taken.size() == 1);
    REQUIRE(taken[0].item_id == 2);
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

TEST_CASE("a caster monster throws its table's own spell", "[combat]") {
    // "Fireball,N,5" at 100%: the name resolves in the spell table, the
    // prose's per-skill dice roll five times, and the target's own fire
    // resistance answers.
    std::string spells_text;
    spells_text += "\t\t\t\r\n";
    spells_text += "#\tFire Spells\t\tRes\tShort Name\tA\tX\tM\tSpell Description\tNormal"
                   "\tExpert\tMaster\r\n";
    spells_text += "6\t6\tFireball\tFire\tFireball\t8\t8\t8"
                   "\tDamage is 1-6 points of damage per point of skill.\tslow\tfast\tfastest"
                   "\r\n";
    data::TextTable text;
    REQUIRE(data::TextTable::parse_body(spells_text, text) == data::TextTableError::None);
    data::SpellStatsTable spells;
    REQUIRE(data::SpellStatsTable::parse(text, spells) == data::SpellStatsError::None);

    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "2d6", "0", "0", "100", "Fireball,N,5");
    REQUIRE(table.entries()[0].spell_percent == 100);
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    std::string cast;
    for (int i = 0; i < 50 && cast.empty(); ++i) {
        const std::string blow = battle.update(0.5f, session, table, spells, party, {0, 0, 0});
        if (blow.find("casts Fireball") != std::string::npos) {
            cast = blow;
        }
    }
    REQUIRE_FALSE(cast.empty());

    // The cast is reported with its spell id, for the bolt and the sound.
    const auto casts = battle.take_casts();
    REQUIRE_FALSE(casts.empty());
    REQUIRE(casts.front().second == 6);

    // Five rolls of 1-6: the wound is at least 5 on somebody.
    int worst = 0;
    for (const auto& who : party) {
        worst = std::max(worst, who.max_hit_points - who.hit_points);
    }
    REQUIRE(worst >= 5);

    // An immune party takes nothing from the same fire.
    std::array<Character, 4> immune{fighter(), fighter(), fighter(), fighter()};
    for (auto& who : immune) {
        who.resistances[static_cast<std::size_t>(data::Resistance::Fire)] =
            data::kResistanceImmune;
    }
    Battle again;
    again.reset(session, table, 9);
    for (int i = 0; i < 50; ++i) {
        (void)again.update(0.5f, session, table, spells, immune, {0, 0, 0});
    }
    for (const auto& who : immune) {
        REQUIRE(who.hit_points == who.max_hit_points);
    }
}

TEST_CASE("a blow that lands on the unconscious kills them", "[combat]") {
    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    // Three already dead, one down: the down one is the only target left,
    // and the first blow that lands finishes them.
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (std::size_t i = 1; i < 4; ++i) {
        party[i].hit_points = 0;
        party[i].affliction = "Dead";
    }
    party[0].hit_points = 0;
    for (int i = 0; i < 200 && !party[0].dead(); ++i) {
        (void)battle.update(0.5f, session, table, {}, party, {0, 0, 0});
    }
    REQUIRE(party[0].dead());
    REQUIRE_FALSE(party[0].can_act());
}

TEST_CASE("a sleeper is struck awake", "[combat]") {
    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "1d2");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (std::size_t i = 1; i < 4; ++i) {
        party[i].hit_points = 0;
        party[i].affliction = "Dead";
    }
    party[0].affliction = "Asleep";
    REQUIRE_FALSE(party[0].can_act());
    for (int i = 0; i < 200 && party[0].hit_points == party[0].max_hit_points; ++i) {
        (void)battle.update(0.5f, session, table, {}, party, {0, 0, 0});
    }
    REQUIRE(party[0].hit_points < party[0].max_hit_points);
    REQUIRE(party[0].affliction.empty());
    REQUIRE(party[0].can_act());
}

TEST_CASE("a party of corpses is not swung at", "[combat]") {
    auto session = with_monster({0, 0, 100});
    const auto table = monsters("20", "0", "2d6");
    Battle battle;
    battle.reset(session, table, 9);

    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (auto& who : party) {
        who.hit_points = 0;
        who.affliction = "Dead";
    }
    for (int i = 0; i < 50; ++i) {
        REQUIRE(battle.update(0.5f, session, table, {}, party, {0, 0, 0}).empty());
    }
}

TEST_CASE("fear, slow, paralysis and charm do what their spells say", "[combat]") {
    const auto session = with_monster({0, 0, 0});
    const auto table = monsters("20", "0", "1d4");
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};

    Battle battle;
    battle.reset(session, table, 5);
    // A feared monster holds its blows until the fear runs out.
    REQUIRE(battle.afflict(0, MonsterCondition::Fear, 10.0f));
    for (int i = 0; i < 19; ++i) {
        REQUIRE(battle.update(0.5f, session, table, {}, party, {0, 0, 0}).empty());
    }
    bool swings_again = false;
    for (int i = 0; i < 40 && !swings_again; ++i) {
        swings_again = !battle.update(0.5f, session, table, {}, party, {0, 0, 0}).empty();
    }
    REQUIRE(swings_again);

    // A paralyzed one neither strikes nor moves; damage does not free it.
    battle.reset(session, table, 5);
    REQUIRE(battle.afflict(0, MonsterCondition::Paralyze, 30.0f));
    REQUIRE_FALSE(battle.can_move(0));
    (void)battle.strike(0, party[0], Pack{}, session, table, items(), {}, {}, {});
    REQUIRE_FALSE(battle.can_move(0));

    // A charmed one is calm exactly until it is hurt. A tougher rat, so the
    // charm-breaking blows do not simply kill it.
    const auto tough = monsters("200", "0", "1d4");
    battle.reset(session, tough, 5);
    REQUIRE(battle.afflict(0, MonsterCondition::Charm, 1000.0f));
    REQUIRE(battle.update(0.5f, session, tough, {}, party, {0, 0, 0}).empty());
    // Strike until a blow lands; a miss does not break the calm.
    for (int i = 0; i < 20 && battle.alive(0); ++i) {
        (void)battle.strike(0, party[0], Pack{}, session, tough, items(), {}, {}, {});
    }
    REQUIRE(battle.alive(0));
    bool hostile = false;
    for (int i = 0; i < 40 && !hostile; ++i) {
        hostile = !battle.update(0.5f, session, tough, {}, party, {0, 0, 0}).empty();
    }
    REQUIRE(hostile);
}

TEST_CASE("only a missile-armed monster attacks from range", "[combat]") {
    // An archer with an Arrow in its Miss column, standing far away.
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\tArcherA\tArcher\t2\t20\t0\t24\t0\t0\tN\tMed\tAggress\t4\t200\t100\t0\t0\tPhys"
            "\t1d4\tArrow\t100\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable archers;
    REQUIRE(data::MonsterStatsTable::parse(table, archers) == data::MonsterStatsError::None);
    REQUIRE(has_missile(archers.entries()[0]));
    REQUIRE(missile_kind(archers.entries()[0]) == "Arrow");

    const auto far_session = with_monster({1200, 0, 0});
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    Battle battle;
    battle.reset(far_session, archers, 5);
    bool shot = false;
    for (int i = 0; i < 10 && !shot; ++i) {
        shot = !battle.update(0.5f, far_session, archers, {}, party, {0, 0, 0}).empty();
    }
    REQUIRE(shot);
    REQUIRE_FALSE(battle.take_shots().empty());

    // The same monster with no missile stays quiet at that distance.
    const auto melee_only = monsters("20", "0", "1d4");
    REQUIRE_FALSE(has_missile(melee_only.entries()[0]));
    Battle idle;
    idle.reset(far_session, melee_only, 5);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(idle.update(0.5f, far_session, melee_only, {}, party, {0, 0, 0}).empty());
    }
}

TEST_CASE("the second attack bites at its own written chance", "[combat]") {
    // A cobra shape: physical fangs, and Att% 100 forcing the poison bite
    // so the test can see it.
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\tCobraA\tCobra\t2\t20\t0\t24\t0\t0\tN\tMed\tAggress\t4\t200\t1\t0\t0\tPhys"
            "\t1d2\t0\t100\tPois\t50d1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable cobras;
    REQUIRE(data::MonsterStatsTable::parse(table, cobras) == data::MonsterStatsError::None);

    const auto session = with_monster({0, 0, 0});
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    for (auto& who : party) {
        who.max_hit_points = 200;
        who.hit_points = 200;
    }
    Battle battle;
    battle.reset(session, cobras, 5);
    // With Att% at 100 every landed blow is the 50-point poison fang, never
    // the 1d2 nibble: any hit that lands takes at least 50.
    for (int i = 0; i < 40; ++i) {
        (void)battle.update(0.5f, session, cobras, {}, party, {0, 0, 0});
    }
    for (const auto& who : party) {
        const int lost = 200 - who.hit_points;
        REQUIRE(lost % 50 == 0);
    }
}

TEST_CASE("a monster with a preference picks its named victims", "[combat]") {
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\tEyeA\tTerrible Eye\t2\t20\t0\t24\t0\t0\tY\tMed\tAggress\t4\t200\t1\t\"D,S\"\t0"
            "\tPhys\t5d1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable eyes;
    REQUIRE(data::MonsterStatsTable::parse(table, eyes) == data::MonsterStatsError::None);
    REQUIRE(eyes.entries()[0].flying);

    const auto session = with_monster({0, 0, 0});
    std::array<Character, 4> party{fighter(), fighter(), fighter(), fighter()};
    party[0].class_name = "Knight";
    party[1].class_name = "Knight";
    party[2].class_name = "Sorcerer";
    party[3].class_name = "Knight";
    for (auto& who : party) {
        who.max_hit_points = 500;
        who.hit_points = 500;
    }
    Battle battle;
    battle.reset(session, eyes, 5);
    for (int i = 0; i < 60; ++i) {
        (void)battle.update(0.5f, session, eyes, {}, party, {0, 0, 0});
    }
    // Every wound sits on the sorcerer; the knights are unmarked.
    REQUIRE(party[2].hit_points < 500);
    REQUIRE(party[0].hit_points == 500);
    REQUIRE(party[1].hit_points == 500);
    REQUIRE(party[3].hit_points == 500);
}
