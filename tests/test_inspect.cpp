// Tests for turning a monster or item row into panel text, and for choosing
// which placed thing the player is looking at.
//
// Hermetic: the rows and the map session are built by hand.
#include <catch2/catch_test_macros.hpp>

#include "core/data/item_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/world/map_session.hpp"
#include "game/inspect.hpp"

using namespace starhaven;
using starhaven::game::inspect;

namespace {

data::MonsterStatsEntry archer() {
    data::MonsterStatsEntry m;
    m.id = 1;
    m.picture = "ArcherA";
    m.name = "Archer";
    m.level = 9;
    m.hit_points = 35;
    m.armor_class = 14;
    m.experience = 171;
    m.attacks[0] = {"Phys", "1D6+1", "Arrow", 0};
    m.resistances[static_cast<std::size_t>(data::Resistance::Fire)] = 10;
    m.resistances[static_cast<std::size_t>(data::Resistance::Cold)] = data::kResistanceImmune;
    return m;
}

// A session holding one monster straight ahead and one item off to the side.
world::MapSession two_things() {
    world::MapSession s;
    s.actors.push_back({"arc1sta", "Archer", 1, {0, 0, -1000}});
    s.objects.push_back({0, "Longsword", 1, {1000, 0, 0}});
    return s;
}

data::MonsterStatsTable monsters_with(const data::MonsterStatsEntry& m) {
    // The table has no public setter, so it is built through its parser from a
    // one-row fixture shaped like MONSTERS.TXT.
    std::string body =
        "#\tPicture\tName\tLVL\tHP\tAC\tEXP\tTreasure\tQuest\tFly\tMove\tAI Type\tHst\tSpd\tRec"
        "\tPref\tBonus\tType\tDamage\tMiss\tAtt%\tType\tDamage\tMiss\tUse%\tSpells\tFire\tElec"
        "\tCold\tPois\tPhys\tMag\tSpecial\r\n";
    body += "1\t" + m.picture + "\t" + m.name + "\t" + std::to_string(m.level) + "\t" +
            std::to_string(m.hit_points) + "\t" + std::to_string(m.armor_class) + "\t" +
            std::to_string(m.experience) +
            "\t0\t0\tN\tShort\tNormal\t4\t140\t90\t0\t0\tPhys\t1D6+1\tArrow\t0\t0\t0\t0\t0\t0"
            "\t10\t0\tImm\t0\t0\t0\t0\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::MonsterStatsTable out;
    REQUIRE(data::MonsterStatsTable::parse(table, out) == data::MonsterStatsError::None);
    return out;
}

}  // namespace

TEST_CASE("a monster row becomes readable lines", "[inspect]") {
    const game::Inspected d = game::describe(archer(), data::SpellStatsTable{});
    REQUIRE(d.title == "Archer");
    REQUIRE_FALSE(d.empty());

    // The numbers a player wants first.
    REQUIRE(d.lines[0].find("level 9") != std::string::npos);
    REQUIRE(d.lines[0].find("35 hit points") != std::string::npos);
    REQUIRE(d.lines[0].find("armour 14") != std::string::npos);
    REQUIRE(d.lines[1].find("171") != std::string::npos);
}

TEST_CASE("an immune resistance says so rather than printing its sentinel", "[inspect]") {
    // kResistanceImmune is -1; showing that would read as a negative percent.
    const game::Inspected d = game::describe(archer(), data::SpellStatsTable{});
    bool found = false;
    for (const auto& line : d.lines) {
        if (line.find("cold immune") != std::string::npos) {
            found = true;
        }
        REQUIRE(line.find("-1") == std::string::npos);
    }
    REQUIRE(found);
}

TEST_CASE("an empty attack slot is not described", "[inspect]") {
    // The second slot is "0" in most rows; printing it would give every
    // monster a phantom attack.
    const game::Inspected d = game::describe(archer(), data::SpellStatsTable{});
    int attacks = 0;
    for (const auto& line : d.lines) {
        if (line.rfind("attack:", 0) == 0) {
            ++attacks;
        }
    }
    REQUIRE(attacks == 1);
}

TEST_CASE("an item row becomes readable lines", "[inspect]") {
    data::ItemStatsEntry item;
    item.id = 1;
    item.name = "Longsword";
    item.value = 50;
    item.equip_stat = "Weapon";
    item.skill_group = "Sword";
    item.modifier_1 = "3d3";

    const game::Inspected d = game::describe(item);
    REQUIRE(d.title == "Longsword");
    REQUIRE(d.lines[0] == "Weapon (Sword)");
    REQUIRE(d.lines[1] == "damage 3d3");
    REQUIRE(d.lines[2] == "50 gold");
}

TEST_CASE("looking at nothing inspects nothing", "[inspect]") {
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    // Facing +z, away from both.
    const game::Inspected d = inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0},
                                      {0, 0, 1}, game::AlwaysVisible{});
    REQUIRE(d.empty());
}

TEST_CASE("looking at a monster inspects it", "[inspect]") {
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0},
                                      {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(d.title == "Archer");
}

TEST_CASE("something too far away is not inspected", "[inspect]") {
    world::MapSession session;
    session.actors.push_back({"arc1sta", "Archer", 1, {0, 0, -100000}});
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0},
                                      {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(d.empty());
}

TEST_CASE("something off to the side is not inspected", "[inspect]") {
    // The aim cone is about twelve degrees; the item sits at ninety.
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0},
                                      {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(d.title != "Longsword");
}

TEST_CASE("a monster with no table row is skipped", "[inspect]") {
    // The map may name a monster id the table does not have; that must not
    // index past the end.
    world::MapSession session;
    session.actors.push_back({"x", "Ghost", 9999, {0, 0, -1000}});
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected d = inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0},
                                      {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(d.empty());
}

TEST_CASE("a monster's spell field is named, not printed raw", "[inspect]") {
    // MONSTERS.TXT writes "Fireball,N,5"; a player wants "Fireball".
    data::MonsterStatsEntry m = archer();
    m.spells = "Fireball,N,5";

    // The heading row names the school in the column the spell rows use for
    // the number within it.
    std::string body =
        "#\tFire Spells\t\tRes\tShort Name\tA\tX\tM\tSpell Description\tNormal\tExpert"
        "\tMaster\r\n"
        "6\t6\tFireball\tFire\tFireball\t8\t8\t8\tBursts into flame.\tslow\tfast"
        "\tfastest\r\n";
    data::TextTable table;
    REQUIRE(data::TextTable::parse_body(body, table) == data::TextTableError::None);
    data::SpellStatsTable spells;
    REQUIRE(data::SpellStatsTable::parse(table, spells) == data::SpellStatsError::None);
    REQUIRE(spells.size() == 1);

    const game::Inspected d = game::describe(m, spells);
    bool found = false;
    for (const auto& line : d.lines) {
        if (line == "casts Fireball") {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("an unrecognised spell field is shown rather than dropped", "[inspect]") {
    data::MonsterStatsEntry m = archer();
    m.spells = "Something,N,5";

    const game::Inspected d = game::describe(m, data::SpellStatsTable{});
    bool found = false;
    for (const auto& line : d.lines) {
        if (line.find("Something") != std::string::npos) {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("something behind a wall is not inspected", "[inspect]") {
    // Aim alone is not enough: the caller decides what can be seen, and a
    // subject it rejects must not be described.
    const auto session = two_things();
    const auto monsters = monsters_with(archer());
    const data::ItemStatsTable items;

    const game::Inspected visible = inspect(session, monsters, items, data::SpellStatsTable{},
                                            {0, 0, 0}, {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(visible.title == "Archer");

    const game::Inspected hidden =
        inspect(session, monsters, items, data::SpellStatsTable{}, {0, 0, 0}, {0, 0, -1},
                [](const starhaven::render::Vec3&) { return false; });
    REQUIRE(hidden.empty());
}

namespace {

// A map with one decoration ahead, listed against the tile it stands on the
// way the shipped maps list theirs.
world::MapSession one_tree() {
    world::MapSession s;
    s.kind = world::MapKind::Outdoor;
    // Render space is (x, up, z) and the map's y is the renderer's z.
    s.decorations.push_back({"tree27", {0, 0, -1000}, 0});

    constexpr int kTiles = world::OdmTileIndex::kDim * world::OdmTileIndex::kDim;
    s.tile_index.entries.clear();
    s.tile_index.starts.assign(static_cast<std::size_t>(kTiles), 0);
    const int listed = world::OdmTileIndex::tile_y_of(-1000.0f) * world::OdmTileIndex::kDim +
                       world::OdmTileIndex::tile_x_of(0.0f);
    for (int t = 0; t < kTiles; ++t) {
        s.tile_index.starts[static_cast<std::size_t>(t)] =
            static_cast<std::uint32_t>(s.tile_index.entries.size());
        if (t == listed) {
            s.tile_index.entries.push_back(world::kPidDecoration);  // id 0
        }
        s.tile_index.entries.push_back(0);
    }
    return s;
}

}  // namespace

TEST_CASE("a decoration the tile index lists can be looked at", "[inspect]") {
    const auto session = one_tree();
    const auto found =
        inspect(session, monsters_with(archer()), data::ItemStatsTable{}, data::SpellStatsTable{},
                {0, 0, 0}, {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(found.title == "tree27");
}

TEST_CASE("a decoration the index does not list is not found", "[inspect]") {
    // The search is the map's own list, not a scan: a decoration standing
    // somewhere the index says nothing about is not a subject.
    auto session = one_tree();
    session.tile_index.entries.clear();
    session.tile_index.starts.clear();
    const auto found =
        inspect(session, monsters_with(archer()), data::ItemStatsTable{}, data::SpellStatsTable{},
                {0, 0, 0}, {0, 0, -1}, game::AlwaysVisible{});
    REQUIRE(found.empty());
}

TEST_CASE("an indoor map has no tile index and keeps its decorations to itself", "[inspect]") {
    auto session = one_tree();
    session.kind = world::MapKind::Indoor;
    REQUIRE(session.decorations_near(0.0f, -1000.0f).empty());
}

namespace {

// A session with one face carrying an event, and a script that names it and
// gives it something to say.
world::MapSession spoken_door() {
    world::MapSession s;
    s.kind = world::MapKind::Indoor;
    s.blv.vertices = {{-100, 0, 0}, {100, 0, 0}, {100, 200, 0}, {-100, 200, 0}};
    world::BlvFace face;
    face.vertex_ids = {0, 1, 2, 3};
    s.blv.faces.push_back(face);
    world::BlvFaceExtra extra;
    extra.face_index = 0;
    extra.event_id = 7;
    s.blv.face_extras.push_back(extra);
    return s;
}

}  // namespace

TEST_CASE("the party can aim at a face that carries an event", "[inspect]") {
    const auto session = spoken_door();
    // The face's centre is at render (0, 0, 100): the vertices' y is the
    // renderer's z.
    const auto aimed = game::aimed_face(session, {0, 0, 400}, {0, 0, -1});
    REQUIRE(aimed.found());
    REQUIRE(aimed.event_id == 7);
    REQUIRE(aimed.index == 0);
}

TEST_CASE("a face out of reach or behind you is not aimed at", "[inspect]") {
    const auto session = spoken_door();
    REQUIRE_FALSE(game::aimed_face(session, {0, 0, 400}, {0, 0, 1}).found());
    REQUIRE_FALSE(game::aimed_face(session, {0, 0, 4000}, {0, 0, -1}).found());
    REQUIRE_FALSE(game::aimed_face(session, {0, 0, 400}, {1, 0, 0}).found());
}

TEST_CASE("an outdoor map has no face events to aim at", "[inspect]") {
    // Only the indoor face carries an event id; the outdoor equivalent is not
    // located. See docs/formats/map-events.md.
    auto session = spoken_door();
    session.kind = world::MapKind::Outdoor;
    REQUIRE_FALSE(game::aimed_face(session, {0, 0, 400}, {0, 0, -1}).found());
}

namespace {

// The 48-byte container the archive wraps a script in, with a zero unpacked
// size so the bytes are read as they are.
std::vector<std::byte> wrap_script(const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> out(48, std::byte{0});
    for (const std::uint8_t b : payload) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

void push_step(std::vector<std::uint8_t>& p, std::uint16_t id, std::uint8_t sequence,
               std::uint8_t opcode, const std::vector<std::uint8_t>& args) {
    p.push_back(static_cast<std::uint8_t>(4 + args.size()));
    p.push_back(static_cast<std::uint8_t>(id & 0xFF));
    p.push_back(static_cast<std::uint8_t>(id >> 8));
    p.push_back(sequence);
    p.push_back(opcode);
    for (const std::uint8_t a : args) {
        p.push_back(a);
    }
}

}  // namespace

TEST_CASE("a face says what its script says", "[inspect]") {
    auto session = spoken_door();

    std::vector<std::uint8_t> code;
    push_step(code, 7, 0, world::kOpcodeName, {1});
    push_step(code, 7, 1, world::kOpcodeMessage, {2, 0, 0, 0});
    REQUIRE(world::MapScript::parse(wrap_script(code), session.script) ==
            world::MapScriptError::None);

    const std::vector<std::uint8_t> text{' ', 0,   'D', 'o', 'o', 'r', 0,   'I', 't', ' ', 'i',
                                         's', ' ', 'l', 'o', 'c', 'k', 'e', 'd', '.', 0};
    REQUIRE(world::MapStrings::parse(wrap_script(text), session.script_strings) ==
            world::MapScriptError::None);

    REQUIRE(game::face_name(session, 7) == "Door");
    REQUIRE(game::face_message(session, 7) == "It is locked.");
    // An event the script does not define says nothing rather than crashing.
    REQUIRE(game::face_name(session, 99).empty());
    REQUIRE(game::face_message(session, 99).empty());
}

TEST_CASE("a face with no name opcode is labelled by its header", "[inspect]") {
    auto session = spoken_door();

    std::vector<std::uint8_t> code;
    // Event 3: a lever, labelled only by its header. Event 4: an
    // establishment, whose header is a 2DEvents row, not a string.
    push_step(code, 3, 0, world::kOpcodeHeader, {2});
    push_step(code, 3, 0, world::kOpcodeDoor, {1, 1});
    push_step(code, 4, 0, world::kOpcodeHeader, {5});
    push_step(code, 4, 0, world::kOpcodeEnter, {5, 0, 0, 0});
    // Event 5: a name opcode outranks the header.
    push_step(code, 5, 0, world::kOpcodeHeader, {2});
    push_step(code, 5, 0, world::kOpcodeName, {3});
    REQUIRE(world::MapScript::parse(wrap_script(code), session.script) ==
            world::MapScriptError::None);

    const std::vector<std::uint8_t> text{' ', 0, 'x', 0,   'L', 'e', 'v', 'e',
                                         'r', 0, 'S', 'i', 'g', 'n', 0};
    REQUIRE(world::MapStrings::parse(wrap_script(text), session.script_strings) ==
            world::MapScriptError::None);

    REQUIRE(game::face_name(session, 3) == "Lever");
    // The establishment's header is not read as a string index; its name
    // comes from the design table, which is the caller's to resolve.
    REQUIRE(game::face_name(session, 4).empty());
    REQUIRE(game::face_name(session, 5) == "Sign");
}

TEST_CASE("outdoors the event sits on a model facet", "[inspect]") {
    // The outdoor counterpart of the indoor face's event id, at +0x124 of the
    // 308-byte facet record. See docs/formats/map-events.md.
    world::MapSession session;
    session.kind = world::MapKind::Outdoor;
    world::OdmModelMesh mesh;
    mesh.vertices = {{-100, 0, 0}, {100, 0, 0}, {100, 0, 200}, {-100, 0, 200}};
    world::OdmModelFacet facet;
    facet.vertex_count = 4;
    facet.vertex_ids = {0, 1, 2, 3};
    facet.event_id = 150;
    mesh.facets.push_back(facet);
    session.meshes.push_back(mesh);

    // The vertices' y is the renderer's z, so the facet's centre is at
    // render (0, 100, 0).
    const auto aimed = game::aimed_face(session, {0, 100, 400}, {0, 0, -1});
    REQUIRE(aimed.found());
    REQUIRE(aimed.event_id == 150);
    REQUIRE_FALSE(game::aimed_face(session, {0, 100, 400}, {0, 0, 1}).found());
}
