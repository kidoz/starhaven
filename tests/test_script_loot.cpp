#include "game/script_loot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <limits>

#include "game/combat.hpp"
#include "game/map_memory.hpp"
#include "game/player.hpp"
#include "game/script_object_effects.hpp"
#include "game/script_walk.hpp"
#include "game/temporary_objects.hpp"

using namespace starhaven;
using namespace starhaven::game;
using Catch::Approx;

namespace {
using Bytes = std::vector<std::byte>;
void put(Bytes& bytes, std::size_t offset, std::uint32_t value, std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i)
        bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8)) & 255U);
}
Bytes compressed(const Bytes& bytes) {
    auto size = compressBound(static_cast<uLong>(bytes.size()));
    Bytes entry(48 + size);
    REQUIRE(compress(reinterpret_cast<Bytef*>(entry.data() + 48), &size,
                     reinterpret_cast<const Bytef*>(bytes.data()),
                     static_cast<uLong>(bytes.size())) == Z_OK);
    entry.resize(48 + size);
    return entry;
}
struct Fixture {
    world::MapSession session;
    data::ItemStatsTable items;
    assets::AssetCache cache;
    Fixture() {
        session.file_name = "Synthetic.blv";
        session.kind = world::MapKind::Indoor;
        session.refill_days = 7;
        Bytes descriptors(4 + 12 * world::kObjectDescriptorSize);
        put(descriptors, 0, 12);
        const std::size_t base = 4 + world::kObjectDescriptorSize;
        put(descriptors, base + 32, 1, 2);
        put(descriptors, base + 34, 2, 2);
        constexpr std::array<std::array<std::uint16_t, 4>, 10> kEffects{
            {
                {1000, 0x194, 0, 768},
                {1050, 0x174, 0, 768},
                {1051, 0x13c, 1, 48},
                {2081, 0x13c, 1, 48},
                {2100, 0x154, 0, 768},
                {2101, 0x13c, 1, 80},
                {4070, 0x54, 0, 256},
                {4071, 0x3c, 1, 80},
                {8080, 0x174, 0, 24},
                {8081, 0x13c, 1, 96},
            },
        };
        for (std::size_t i = 0; i < kEffects.size(); ++i) {
            const auto offset = 4 + (i + 2) * world::kObjectDescriptorSize;
            put(descriptors, offset + 32, kEffects[i][0], 2);
            put(descriptors, offset + 34, 2, 2);
            put(descriptors, offset + 38, kEffects[i][1], 2);
            put(descriptors, offset + 40, kEffects[i][2], 2);
            put(descriptors, offset + 42, kEffects[i][3], 2);
        }
        REQUIRE(world::ObjectTable::parse(compressed(descriptors), session.object_descriptors) ==
                world::ObjectTableError::None);
        Bytes frames(8 + 2 * world::kSpriteFrameSize + 4);
        put(frames, 0, 2);
        put(frames, 4, 2);
        for (std::size_t i = 0; i < 2; ++i) {
            const auto offset = 8 + i * world::kSpriteFrameSize;
            frames[offset] = i == 0 ? std::byte{'a'} : std::byte{'c'};
            frames[offset + 12] = i == 0 ? std::byte{'b'} : std::byte{'d'};
            put(frames, offset + 40, 65536);
            put(frames, offset + 44, 4);
            put(frames, 8 + 2 * world::kSpriteFrameSize + i * 2, static_cast<std::uint32_t>(i), 2);
        }
        REQUIRE(world::SpriteFrameTable::parse(compressed(frames), session.sprite_frames) ==
                world::SpriteFrameError::None);
        data::TextTable text;
        REQUIRE(data::TextTable::parse_body(
                    "\r\nItem #\tPic File\tName\tValue\tEquip Stat\tSkill "
                    "Group\tMod1\tMod2\tmaterial\tID/Rep/St\tNot identified name\tSprite "
                    "Index\tShape\tEquip X\tEquip Y\tNotes\r\n"
                    "0\t\t\t0\t\t\t0\t0\t0\t0\t\t0\t0\t0\t0\t\r\n"
                    "1\tmissing-art\tSynthetic "
                    "loot\t1\tWeapon\tSword\t1d1\t0\t0\t1\tUnknown\t1\t1\t0\t0\t\r\n",
                    text) == data::TextTableError::None);
        REQUIRE(data::ItemStatsTable::parse(text, items) == data::ItemStatsError::None);
        const std::array floor{
            render::Vec3{-1000, 0, -1000},
            render::Vec3{1000, 0, -1000},
            render::Vec3{1000, 0, 1000},
            render::Vec3{-1000, 0, 1000},
        };
        session.collision.add_polygon(floor, {0, 1, 0});
    }
};
world::ObjectSpawnRequest request() {
    world::ObjectSpawnRequest spawn;
    spawn.object_id = 1;
    spawn.x = 10;
    spawn.y = 20;
    spawn.z = 100;
    spawn.count = 1;
    return spawn;
}
world::MapScript script() {
    Bytes bytes(48, std::byte{0});
    const auto add = [&](std::uint8_t sequence, std::uint8_t opcode, Bytes args) {
        bytes.push_back(static_cast<std::byte>(4 + args.size()));
        bytes.push_back(std::byte{9});
        bytes.push_back(std::byte{0});
        bytes.push_back(static_cast<std::byte>(sequence));
        bytes.push_back(static_cast<std::byte>(opcode));
        bytes.insert(bytes.end(), args.begin(), args.end());
    };
    Bytes spawn(22);
    put(spawn, 0, 1);
    put(spawn, 12, 100);
    spawn[20] = std::byte{1};
    add(0, world::kOpcodeSpawnObjects, spawn);
    add(1, world::kOpcodeShowMessage, {});
    add(2, world::kOpcodeSpawnObjects, spawn);
    add(3, world::kOpcodeEnd, {});
    world::MapScript result;
    REQUIRE(world::MapScript::parse(bytes, result) == world::MapScriptError::None);
    return result;
}
}  // namespace

TEST_CASE("spawned loot moves, rests and persists without temporary expiry", "[script-loot]") {
    const Fixture f;
    ScriptLootState state;
    REQUIRE(spawn_script_loot(request(), f.session, f.items, state).created == 1);
    REQUIRE(valid_script_loot(state, f.session, f.items));
    REQUIRE(state.objects.front().position.x == 10);
    REQUIRE(state.objects.front().position.y == 100);
    REQUIRE(state.objects.front().position.z == 20);
    advance_script_loot(state, 0, f.session);
    REQUIRE(state.objects.front().position.y == 100);
    for (int i = 0; i < 300; ++i)
        advance_script_loot(state, 1, f.session);
    REQUIRE(state.objects.size() == 1);
    REQUIRE(state.objects.front().resting);
    REQUIRE(state.objects.front().position.y == Approx(-0.99f).margin(0.02f));
    REQUIRE(render::length(state.objects.front().velocity) == 0);
}
TEST_CASE("spawn requests preserve repeats, capacity, failure atomicity and scatter RNG",
          "[script-loot]") {
    Fixture f;
    ScriptLootState state;
    auto req = request();
    req.scatter = true;
    req.speed = -128;
    req.count = 2;
    Mm6Random expected{state.random};
    for (int i = 0; i < 4; ++i)
        (void)expected.next();
    REQUIRE(spawn_script_loot(req, f.session, f.items, state).created == 2);
    REQUIRE(state.random == expected.state());
    REQUIRE(state.objects.front().velocity.y < 0);
    REQUIRE(spawn_script_loot(req, f.session, f.items, state).created == 2);
    f.session.objects.resize(kTemporaryObjectCapacity);
    for (auto& object : f.session.objects)
        object.descriptor_index = 1;
    expected = Mm6Random{state.random};
    for (int i = 0; i < 4; ++i)
        (void)expected.next();
    const auto full = spawn_script_loot(req, f.session, f.items, state);
    REQUIRE(full.created == 0);
    REQUIRE(full.dropped == 2);
    REQUIRE(state.random == expected.state());
    req.count = 0;
    REQUIRE(spawn_script_loot(req, f.session, f.items, state).dropped == 0);
    REQUIRE(state.random == expected.state());
    req.object_id = 0x10001;
    REQUIRE(spawn_script_loot(req, f.session, f.items, state).error ==
            LootSpawnError::UnsupportedId);
    req = request();
    f.session.sprite_frames = {};
    REQUIRE(spawn_script_loot(req, f.session, f.items, state).error ==
            LootSpawnError::MissingResource);
    REQUIRE(state.objects.size() == 4);
    REQUIRE(state.random == expected.state());
}
TEST_CASE("modal continuation never repeats a previous spawn", "[script-loot]") {
    const Fixture f;
    ScriptLootState loot;
    WalkState state;
    WalkPresentation text;
    const auto event = script();
    auto outcome = walk_event(event, 9, state, -1, f.session.file_name, &text);
    REQUIRE(outcome.unsupported.empty());
    REQUIRE(outcome.acted());
    REQUIRE(outcome.object_spawns.size() == 1);
    if (!outcome.message) {
        FAIL("spawn must suspend at the modal");
        return;
    }
    REQUIRE(spawn_script_loot(outcome.object_spawns.front().request, f.session, f.items, loot)
                .created == 1);
    outcome = walk_event(event, 9, state, outcome.message->resume_at, f.session.file_name, &text);
    REQUIRE(outcome.object_spawns.size() == 1);
    REQUIRE(spawn_script_loot(outcome.object_spawns.front().request, f.session, f.items, loot)
                .created == 1);
    REQUIRE(loot.objects.size() == 2);
}
TEST_CASE("full packs, travel and save reload preserve loot without duplicate pickup",
          "[script-loot]") {
    Fixture f;
    ScriptLootState loot;
    REQUIRE(spawn_script_loot(request(), f.session, f.items, loot).created == 1);
    std::array<Pack, 4> packs;
    for (auto& pack : packs)
        REQUIRE(pack.add(1, kPackWidth, kPackHeight));
    const render::Vec3 party{10, 120, 20};
    REQUIRE(take_script_loot(loot, f.session, f.items, f.cache, party, packs).empty());
    REQUIRE(loot.objects.size() == 1);
    advance_script_loot(loot, 0.001, f.session);
    Battle battle;
    auto memory = capture_map_memory(f.session, battle, {}, 2, loot);
    SaveState save;
    save.map_file = f.session.file_name;
    save.remembered = save_map_memories({}, save.map_file, memory);
    SaveState loaded;
    REQUIRE(parse_save(save_text(save), loaded));
    auto memories = load_map_memories(loaded.remembered);
    ScriptLootState restored;
    std::set<int> chests;
    REQUIRE(restore_map_memory(memories.at("synthetic.blv"), MapMemoryUse::SavedSnapshot, 100,
                               f.session, battle, chests, {},
                               &restored) == MapMemoryResult::Restored);
    REQUIRE(restored.objects.size() == 1);
    REQUIRE(restored.tick_remainder == loot.tick_remainder);
    REQUIRE(restored.random == loot.random);
    packs.front().clear();
    REQUIRE(take_script_loot(restored, f.session, f.items, f.cache, {900, 120, 20}, packs).empty());
    REQUIRE(take_script_loot(restored, f.session, f.items, f.cache, party, packs) ==
            std::vector<int>{1});
    REQUIRE(restored.objects.empty());
    REQUIRE_FALSE(packs.front().items().front().identified);
    memory = capture_map_memory(f.session, battle, {}, 2, restored);
    save.remembered = save_map_memories(memories, "SYNTHETIC.BLV", memory);
    REQUIRE(parse_save(save_text(save), loaded));
    memories = load_map_memories(loaded.remembered);
    REQUIRE(restore_map_memory(memories.at("synthetic.blv"), MapMemoryUse::Revisit, 3, f.session,
                               battle, chests, {}, &restored) == MapMemoryResult::Restored);
    REQUIRE(take_script_loot(restored, f.session, f.items, f.cache, party, packs).empty());
    REQUIRE(packs.front().size() == 1);
    REQUIRE(restore_map_memory(capture_map_memory(f.session, battle, {}, 2, loot),
                               MapMemoryUse::Revisit, 9, f.session, battle, chests, {},
                               &restored) == MapMemoryResult::Expired);
    REQUIRE(restored.objects.empty());
}
TEST_CASE("a wall blocks spawned loot pickup", "[script-loot]") {
    Fixture f;
    ScriptLootState loot;
    REQUIRE(spawn_script_loot(request(), f.session, f.items, loot).created == 1);
    const std::array wall{
        render::Vec3{0, 0, -100},
        render::Vec3{0, 300, -100},
        render::Vec3{0, 300, 100},
        render::Vec3{0, 0, 100},
    };
    f.session.collision.add_polygon(wall, {1, 0, 0});
    std::array<Pack, 4> packs;
    REQUIRE(take_script_loot(loot, f.session, f.items, f.cache, {-10, 120, 20}, packs).empty());
    REQUIRE(loot.objects.size() == 1);
}

TEST_CASE("save reload resumes fractional motion and the next scatter sequence", "[script-loot]") {
    const Fixture f;
    ScriptLootState live;
    auto spawn = request();
    spawn.scatter = true;
    spawn.speed = 500;
    REQUIRE(spawn_script_loot(spawn, f.session, f.items, live).created == 1);
    advance_script_loot(live, 0.126, f.session);
    SaveState saved;
    saved.map_file = f.session.file_name;
    SaveState::RememberedMap map;
    map.file = saved.map_file;
    map.loot = live;
    saved.remembered.push_back(map);
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    auto resumed = loaded.remembered.front().loot;
    REQUIRE(valid_script_loot(resumed, f.session, f.items));
    advance_script_loot(live, 0.25, f.session);
    advance_script_loot(resumed, 0.25, f.session);
    REQUIRE(spawn_script_loot(spawn, f.session, f.items, live).created == 1);
    REQUIRE(spawn_script_loot(spawn, f.session, f.items, resumed).created == 1);
    REQUIRE(live.random == resumed.random);
    REQUIRE(live.tick_remainder == resumed.tick_remainder);
    REQUIRE(live.objects.size() == resumed.objects.size());
    for (std::size_t i = 0; i < live.objects.size(); ++i) {
        REQUIRE(live.objects[i].position.x == resumed.objects[i].position.x);
        REQUIRE(live.objects[i].position.y == resumed.objects[i].position.y);
        REQUIRE(live.objects[i].position.z == resumed.objects[i].position.z);
        REQUIRE(live.objects[i].velocity.x == resumed.objects[i].velocity.x);
        REQUIRE(live.objects[i].velocity.y == resumed.objects[i].velocity.y);
        REQUIRE(live.objects[i].velocity.z == resumed.objects[i].velocity.z);
    }
}
TEST_CASE("loot save validation is atomic and version six stays readable", "[script-loot]") {
    SaveState saved;
    saved.map_file = "synthetic.blv";
    SaveState::RememberedMap map;
    map.file = saved.map_file;
    map.loot.objects.push_back({1, 1, {10, 20, 30}, {1, 2, 3}, false});
    saved.remembered.push_back(map);
    const auto text = save_text(saved);
    const auto at = text.find("recall\t");
    const auto end = text.find('\n', at);
    REQUIRE(at != std::string::npos);
    SaveState sentinel;
    sentinel.gold = 123;
    for (std::size_t cut = at + 7; cut < end; ++cut) {
        if (text[cut] != '\t')
            continue;
        auto truncated = text;
        truncated.erase(cut, end - cut);
        REQUIRE_FALSE(parse_save(truncated, sentinel));
        REQUIRE(sentinel.gold == 123);
    }
    saved.remembered.front().loot.tick_remainder = 1;
    REQUIRE_FALSE(parse_save(save_text(saved), sentinel));
    saved.remembered.front().loot.tick_remainder = 0;
    saved.remembered.front().loot.objects.front().item_id = -1;
    REQUIRE_FALSE(parse_save(save_text(saved), sentinel));
    REQUIRE(sentinel.gold == 123);
    auto old = text;
    old.replace(old.find('\t') + 1, 1, "6");
    old.replace(at, end - at, "recall\tsynthetic.blv\t0\t0\t0\t0");
    REQUIRE(parse_save(old, sentinel));
    REQUIRE(sentinel.remembered.front().loot.objects.empty());
}

TEST_CASE("live event effects share capacity and random ordering with persistent loot",
          "[script-loot][script-object-effects]") {
    Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    // One free slot shared by placed objects, loot and temporary effects.
    f.session.objects.resize(999);
    for (auto& object : f.session.objects)
        object.descriptor_index = 1;
    auto req = request();
    req.object_id = 1000;
    req.scatter = true;
    Mm6Random expected{loot.random};
    (void)expected.next();
    (void)expected.next();
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    REQUIRE(loot.random == expected.state());
    const auto before_failure = loot.random;
    REQUIRE(effects.spawn(req, {}, f.items, loot).error == LootSpawnError::MissingResource);
    REQUIRE(loot.random == before_failure);
    REQUIRE(effects.active_count() == 1);
    req.object_id = 1;
    const auto full = effects.spawn(req, f.session, f.items, loot);
    REQUIRE(full.error == LootSpawnError::None);
    REQUIRE(full.created == 0);
    REQUIRE(full.dropped == 1);
    (void)expected.next();
    (void)expected.next();
    REQUIRE(loot.random == expected.state());
    REQUIRE(loot.objects.empty());
    effects.clear();
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    req.object_id = 1050;
    REQUIRE(effects.spawn(req, f.session, f.items, loot).dropped == 1);
    REQUIRE(effects.active_count() == 0);
    req.object_id = 65535;
    const auto before = loot.random;
    REQUIRE(effects.spawn(req, f.session, f.items, loot).error == LootSpawnError::UnsupportedId);
    REQUIRE(loot.random == before);
    REQUIRE(loot.objects.size() == 1);
}

TEST_CASE("temporary event effects use paused simulation time and reset their impact animation",
          "[script-object-effects]") {
    const Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    auto req = request();
    req.object_id = 1050;
    req.z = 1;
    req.speed = -128;
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "a");
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    (void)effects.advance(0.5 / 128, f.session);
    (void)effects.advance(0, f.session);
    (void)effects.advance(-1, f.session);
    (void)effects.advance(std::numeric_limits<double>::infinity(), f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 1);
    REQUIRE(effects.advance(0.5 / 128, f.session).detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 0);
    REQUIRE(effects.advance(1.0 / 128, f.session).detonations.size() == 1);
    const auto impact = effects.sprites(f.session.sprite_frames);
    REQUIRE(impact.size() == 1);
    REQUIRE(impact.front().animation == "c");
    REQUIRE(impact.front().animation_ticks == 0);
    REQUIRE(impact.front().position.y == Approx(-1));
    (void)effects.advance(8.0 / 128, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 1);
    (void)effects.advance(0, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 1);
    REQUIRE(effects.advance(40.0 / 128, f.session).expired == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.empty());
}

TEST_CASE("temporary event application supports continuation and map-boundary clearing",
          "[script-object-effects]") {
    const Fixture f;
    // The same synthetic event with temporary rather than persistent objects.
    world::MapScript event;
    // Build from a numeric EVT record to exercise dispatch through the live seam.
    Bytes bytes(48, std::byte{0});
    bytes.insert(bytes.end(),
                 {std::byte{26}, std::byte{1}, std::byte{0}, std::byte{0}, std::byte{34}});
    Bytes args(22);
    put(args, 0, 1000);
    put(args, 12, 100);
    args[20] = std::byte{1};
    bytes.insert(bytes.end(), args.begin(), args.end());
    bytes.insert(bytes.end(),
                 {std::byte{4}, std::byte{1}, std::byte{0}, std::byte{1}, std::byte{33}});
    REQUIRE(world::MapScript::parse(bytes, event) == world::MapScriptError::None);
    WalkState walk;
    const auto first = walk_event(event, 1, walk);
    REQUIRE(first.object_spawns.size() == 1);
    REQUIRE(first.message.has_value());
    ScriptLootState loot;
    ScriptObjectEffects effects;
    REQUIRE(effects.spawn(first.object_spawns.front().request, f.session, f.items, loot).created ==
            1);
    if (first.message) {
        const auto resumed = walk_event(event, 1, walk, first.message->resume_at);
        REQUIRE(resumed.object_spawns.empty());
    }
    (void)effects.advance(0.5 / 128, {});
    effects.clear();
    REQUIRE(effects.active_count() == 0);
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(effects.spawn(first.object_spawns.front().request, f.session, f.items, loot).created ==
            1);
    (void)effects.advance(0.5 / 128, {});
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 100);
    (void)effects.advance(0.5 / 128, {});
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y < 100);
    // Large deltas are bounded to the same one-second quantum as persistent loot.
    REQUIRE(effects.advance(1000, {}).expired == 0);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 16);
}

TEST_CASE("2081 animation and loot coexist with separate expiration and map lifecycles",
          "[script-object-effects]") {
    const Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    auto effect = request();
    effect.object_id = 2081;
    effect.speed = 1000;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.spawn(request(), f.session, f.items, loot).created == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "c");
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    (void)effects.advance(0, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 100);
    (void)effects.advance(8.0 / 128, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == Approx(162.5));
    REQUIRE(effects.advance(39.0 / 128, f.session).expired == 0);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 5);
    const auto end = effects.advance(1.0 / 128, f.session);
    REQUIRE(end.expired == 1);
    REQUIRE(end.detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.size() == 1);
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    effects.clear();
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.size() == 1);
    REQUIRE(valid_script_loot(loot, f.session, f.items));
}

TEST_CASE("2100 live presentation switches to 2101 and expires on simulation time",
          "[script-object-effects]") {
    const Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    auto effect = request();
    effect.object_id = 2100;
    effect.z = 10;
    effect.speed = -30000;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "a");
    (void)effects.advance(0, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 10);
    REQUIRE(effects.advance(1.0 / 128, f.session).detonations.size() == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "c");
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    const auto at = effects.sprites(f.session.sprite_frames).front().position;
    (void)effects.advance(8.0 / 128, {});
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 1);
    REQUIRE(render::length(effects.sprites(f.session.sprite_frames).front().position - at) == 0);
    REQUIRE(effects.advance(72.0 / 128, {}).expired == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.empty());
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    effects.clear();
    REQUIRE(effects.active_count() == 0);
}

TEST_CASE("4070 live effects retain flight frames on contact and reset animation at timeout",
          "[script-object-effects]") {
    const Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    auto effect = request();
    effect.object_id = 4070;
    effect.z = 10;
    effect.speed = -30000;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(1.0 / 128, f.session).detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "a");
    (void)effects.advance(0, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    REQUIRE(effects.advance(1, f.session).detonations.empty());
    REQUIRE(effects.advance(126.0 / 128, f.session).detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 31);
    REQUIRE(effects.advance(1.0 / 128, f.session).detonations.size() == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "c");
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 0);
    const auto at = effects.sprites(f.session.sprite_frames).front().position;
    REQUIRE(effects.advance(79.0 / 128, {}).expired == 0);
    REQUIRE(render::length(effects.sprites(f.session.sprite_frames).front().position - at) == 0);
    REQUIRE(effects.advance(1.0 / 128, {}).expired == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.empty());
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    effects.clear();
    REQUIRE(effects.active_count() == 0);
}

TEST_CASE("outdoor terrain supports live effects and persistent loot", "[script-object-effects]") {
    Fixture f;
    f.session.kind = world::MapKind::Outdoor;
    f.session.collision = {};
    f.session.terrain.heightmap.fill(4);  // 128 world units
    ScriptLootState loot;
    ScriptObjectEffects effects;
    auto effect = request();
    effect.object_id = 4070;
    effect.z = 200;
    effect.speed = -30000;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(0, f.session).terrain_contacts == 0);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 200);
    const auto contact = effects.advance(1.0 / 128, f.session);
    REQUIRE(contact.terrain_contacts == 1);
    REQUIRE(contact.detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == Approx(127.01));
    REQUIRE(effects.advance(1, f.session).detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == Approx(127.01));
    REQUIRE(effects.advance(127.0 / 128, f.session).detonations.size() == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "c");
    REQUIRE(effects.advance(80.0 / 128, f.session).expired == 1);

    effect.object_id = 1050;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(1.0 / 128, f.session).detonations.size() == 1);
    effects.clear();
    effect.object_id = 1000;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(1.0 / 128, f.session).terrain_contacts == 1);
    const auto bounced = effects.sprites(f.session.sprite_frames).front().position.y;
    (void)effects.advance(1.0 / 128, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y > bounced);
    effects.clear();

    effect.object_id = 1;
    REQUIRE(spawn_script_loot(effect, f.session, f.items, loot).created == 1);
    advance_script_loot(loot, 1.0 / 128, f.session);
    REQUIRE(loot.objects.front().resting);
    REQUIRE(loot.objects.front().position.y == Approx(127.01));
    advance_script_loot(loot, 1, f.session);
    REQUIRE(loot.objects.front().position.y == Approx(127.01));
    // Terrain is read from the current session, not cached across map changes.
    f.session.terrain.heightmap.fill(0);
    advance_script_loot(loot, 1.0 / 128, f.session);
    REQUIRE_FALSE(loot.objects.front().resting);
    REQUIRE(loot.objects.front().position.y < 127.01f);

    // The same height data must not turn into a phantom floor indoors.
    f.session.kind = world::MapKind::Indoor;
    effect.object_id = 4070;
    REQUIRE(effects.spawn(effect, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(1.0 / 128, f.session).terrain_contacts == 0);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y < 0);
}

TEST_CASE("8080 live effects animate until contact or expiry and leave persistent loot intact",
          "[script-object-effects]") {
    Fixture f;
    ScriptLootState loot;
    ScriptObjectEffects effects;
    REQUIRE(effects.spawn(request(), f.session, f.items, loot).created == 1);
    auto req = request();
    req.object_id = 8080;
    req.speed = 128;
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation == "a");
    (void)effects.advance(0, f.session);
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == 100);
    REQUIRE(effects.advance(8.0 / 128, f.session).detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).front().position.y == Approx(108));
    REQUIRE(effects.sprites(f.session.sprite_frames).front().animation_ticks == 1);
    REQUIRE(effects.advance(15.0 / 128, f.session).expired == 0);
    const auto timeout = effects.advance(1.0 / 128, f.session);
    REQUIRE(timeout.expired == 1);
    REQUIRE(timeout.detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(loot.objects.size() == 1);

    req.z = 10;
    req.speed = -30000;
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    REQUIRE(effects.advance(1.0 / 128, f.session).expired == 1);
    f.session.kind = world::MapKind::Outdoor;
    f.session.collision = {};
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    const auto terrain = effects.advance(1.0 / 128, f.session);
    REQUIRE(terrain.terrain_contacts == 1);
    REQUIRE(terrain.expired == 1);
    REQUIRE(terrain.detonations.empty());
    REQUIRE(effects.sprites(f.session.sprite_frames).empty());
    REQUIRE(effects.spawn(req, f.session, f.items, loot).created == 1);
    effects.clear();
    REQUIRE(effects.active_count() == 0);
    REQUIRE(loot.objects.size() == 1);
}

TEST_CASE("live 8080 actor contact uses combat state, magic resistance and saved random state",
          "[script-loot]") {
    Fixture f;
    data::TextTable text;
    REQUIRE(data::TextTable::parse_body("#\tPicture\tName\tLVL\tHP\tMag\tFire\r\n"
                                        "1\tsynthetic\tSynthetic\t0\t100\t0\t200\r\n",
                                        text) == data::TextTableError::None);
    data::MonsterStatsTable monsters;
    REQUIRE(data::MonsterStatsTable::parse(text, monsters) == data::MonsterStatsError::None);
    f.session.actors.push_back({"synthetic", "Synthetic", 1, {10, 100, 20}});
    Battle battle;
    battle.reset(f.session, monsters, 1);
    battle.hold_slot(0, 0, 100);
    battle.afflict(0, MonsterCondition::Paralyze, 100);
    // A small wound puts the actor into Wince before the state reset.
    const data::SpellRange wound{1, 1};
    const data::SpellRange no_scaling;
    const data::RandomItemTable random_items;
    const data::StandardBonusTable standard;
    const data::SpecialBonusTable special;
    (void)battle.smite(0, wound, no_scaling, 0, "Ener", "Synthetic", f.session, monsters, f.items,
                       random_items, standard, special);
    REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Wince);
    const auto health = battle.health_of(0);
    bool dead = false;
    SECTION("accepted magic gate resets animation without healing or curing") {}
    SECTION("dead actors are not contacted") {
        battle.kill(0);
        dead = true;
    }
    ScriptLootState loot;
    ScriptObjectEffects live;
    auto req = request();
    req.object_id = 8080;
    REQUIRE(live.spawn(req, f.session, f.items, loot).created == 1);
    Mm6Random expected{loot.random};
    if (!dead)
        (void)expected.next();
    REQUIRE(live.advance(0, f.session, battle, monsters, loot).actor_contacts == 0);
    const auto step = live.advance(1.0 / 128, f.session, battle, monsters, loot);
    REQUIRE(step.actor_contacts == (dead ? 0U : 1U));
    REQUIRE(step.actor_accepted == (dead ? 0U : 1U));
    REQUIRE(step.detonations.empty());
    REQUIRE(loot.random == expected.state());
    if (!dead) {
        REQUIRE(battle.animation_of(0) == world::MonsterAnimation::Stand);
        REQUIRE(battle.health_of(0) == health);
        REQUIRE(battle.slot_up(0, 0));
        REQUIRE_FALSE(battle.can_move(0));
        REQUIRE(live.sprites(f.session.sprite_frames).front().animation == "c");
        REQUIRE(live.advance(95.0 / 128, f.session, battle, monsters, loot).expired == 0);
        REQUIRE(live.advance(1.0 / 128, f.session, battle, monsters, loot).expired == 1);
        REQUIRE(loot.random == expected.state());
    }
    live.clear();
    REQUIRE(live.active_count() == 0);
}

TEST_CASE("live 4070 touches actors and the party without damage or resistance draws",
          "[script-loot]") {
    Fixture f;
    data::TextTable text;
    REQUIRE(data::TextTable::parse_body("#\tPicture\tName\tLVL\tHP\tMag\r\n"
                                        "1\tsynthetic\tSynthetic\t100\t100\tImm\r\n",
                                        text) == data::TextTableError::None);
    data::MonsterStatsTable monsters;
    REQUIRE(data::MonsterStatsTable::parse(text, monsters) == data::MonsterStatsError::None);
    f.session.actors.push_back({"synthetic", "Synthetic", 1, {10, 100, 20}});
    Battle battle;
    battle.reset(f.session, monsters, 1);
    battle.hold_slot(0, 0, 100);
    battle.afflict(0, MonsterCondition::Paralyze, 100);
    const auto health = battle.health_of(0);
    bool actor_hit = true;
    bool party_hit = false;
    std::optional<render::Vec3> eye;
    SECTION("immune living actor still detonates 4070") {}
    SECTION("dead actor is excluded") {
        battle.kill(0);
        actor_hit = false;
    }
    SECTION("party body is measured from feet below its eye") {
        battle.kill(0);
        actor_hit = false;
        party_hit = true;
        eye = render::Vec3{10, 100 + kEyeHeight, 20};
    }
    SECTION("party outside the projectile path is excluded") {
        battle.kill(0);
        actor_hit = false;
        eye = render::Vec3{1000, 100 + kEyeHeight, 20};
    }
    ScriptLootState loot;
    ScriptObjectEffects live;
    auto req = request();
    req.object_id = 4070;
    REQUIRE(live.spawn(req, f.session, f.items, loot).created == 1);
    const auto random = loot.random;
    REQUIRE(live.advance(0, f.session, battle, monsters, loot, eye).detonations.empty());
    const auto step = live.advance(1.0 / 128, f.session, battle, monsters, loot, eye);
    REQUIRE(step.actor_contacts == (actor_hit ? 1U : 0U));
    REQUIRE(step.party_contacts == (party_hit ? 1U : 0U));
    REQUIRE(step.actor_accepted == 0);
    REQUIRE(step.detonations.size() == (actor_hit || party_hit ? 1U : 0U));
    REQUIRE(loot.random == random);
    if (actor_hit) {
        REQUIRE(battle.health_of(0) == health);
        REQUIRE(battle.slot_up(0, 0));
        REQUIRE_FALSE(battle.can_move(0));
    }
    if (actor_hit || party_hit) {
        REQUIRE(live.sprites(f.session.sprite_frames).front().animation == "c");
        REQUIRE(live.sprites(f.session.sprite_frames).front().animation_ticks == 0);
        REQUIRE(live.advance(79.0 / 128, f.session, battle, monsters, loot, eye).expired == 0);
        REQUIRE(live.advance(1.0 / 128, f.session, battle, monsters, loot, eye).expired == 1);
        REQUIRE(loot.random == random);
    }
    live.clear();
    REQUIRE(live.active_count() == 0);
}
