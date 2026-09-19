// Reads one map's event script and strings from your own legal install.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "core/assets/asset_cache.hpp"
#include "core/data/building_stats.hpp"
#include "core/data/game_data.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/npc_stats.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/map_script.hpp"
#include "core/world/map_session.hpp"
#include "game/combat.hpp"
#include "game/map_memory.hpp"
#include "game/player.hpp"
#include "game/save.hpp"
#include "game/script_coverage.hpp"
#include "game/script_decorations.hpp"
#include "game/script_faces.hpp"
#include "game/script_loot.hpp"
#include "game/script_object_effects.hpp"
#include "game/script_objects.hpp"
#include "game/script_walk.hpp"
#include "game/shop.hpp"
#include "game/sprites.hpp"
#include "game/temporary_objects.hpp"
#include "game/travel.hpp"
#include <set>

#include "core/image/zlib_util.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/object_table.hpp"
#include "core/world/sound_table.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--scan | --coverage [--strict] | <map stem> [event]>\n"
              << "\n"
              << "Prints a map's .EVT script and .STR strings from icons.lod.\n"
              << "  --coverage [--strict]  metadata-only TSV dispatch audit of every .EVT;\n"
              << "           --strict also fails on unsupported or short-argument records\n"
              << "  --face-bits  verify opcode 23 masks and indoor face lifecycles\n"
              << "  --object-spawns  audit opcode 34 operands and object/frame/item joins\n"
              << "  --object-loot  verify CD2 effects, loot pickup, map memory and saves\n"
              << "  --object-contacts verify controlled 4070 actor/party impacts\n"
              << "  --object-actor    verify controlled 8080 actor contacts and 8081 resources\n"
              << "  --object-removal  verify OUTD3 event 200 non-actor 8080 lifecycle\n"
              << "  --object-expiry  verify OUTE3 event 220 timed object replacement\n"
              << "  --object-impact  verify D01 event 47 object impact and replacement\n"
              << "  --object-lifecycle  walk D18 event 56 spawn branches through live effects\n"
              << "  --decoration-events  verify opcode 42 and default decoration interactions\n"
              << "  --generated-items  verify opcode 41 generation against item tables\n"
              << "  --messages  verify opcode 33 suspension and preceding text joins\n"
              << "  --decorations  verify opcode 13 layouts and joins against loaded maps\n"
              << "\n"
              << "  --actor-timers  every map's actor block, at the three\n"
              << "           64-bit fields the AI reads and nothing writes\n"
              << "  --scan   research mode: every opcode across all scripts, its\n"
              << "           argument sizes, and any map file name in its arguments\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar << " to the install directory.\n";
}

// Two of the 83 scripts ship with a lowercase extension — D08.evt and
// Pyramid.evt — so the extension test must not care about case.
bool is_script(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    const std::string tail = name.substr(name.size() - 4);
    return tail[0] == '.' && std::tolower(static_cast<unsigned char>(tail[1])) == 'e' &&
           std::tolower(static_cast<unsigned char>(tail[2])) == 'v' &&
           std::tolower(static_cast<unsigned char>(tail[3])) == 't';
}

// Audit opcode 34 without walking events or mutating maps. Numeric metadata
// distinguishes decoded requests from live runtime support.
int do_object_spawns(const starhaven::lod::LodArchive& icons,
                     const std::filesystem::path& data_dir) {
    using namespace starhaven;
    world::ObjectTable objects;
    world::SpriteFrameTable frames;
    data::ItemStatsTable items;
    std::span<const std::byte> raw;
    if (!game::audit_script_coverage(icons).complete() ||
        icons.payload("DOBJLIST.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::ObjectTable::parse(raw, objects) != world::ObjectTableError::None ||
        icons.payload("DSFT.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SpriteFrameTable::parse(raw, frames) != world::SpriteFrameError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: incomplete script or object-spawn input\n";
        return 1;
    }
    std::size_t records = 0;
    std::size_t short_records = 0;
    std::size_t resolved = 0;
    std::size_t requested = 0;
    std::size_t failures = 0;
    std::set<std::string> scripts;
    std::set<std::pair<std::string, std::uint16_t>> events;
    std::cout << "script\tevent\tsequence\tstatus\tobject\tx\ty\tz\tspeed\tcount\tscatter"
                 "\tdescriptor\tflags\tlifetime\tframe\tgroup_length\titem\n";
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name))
            continue;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None)
            return 1;
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeSpawnObjects)
                continue;
            ++records;
            scripts.insert(entry.name);
            events.emplace(entry.name, step.event_id);
            std::cout << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t';
            const auto request = world::parse_object_spawn(step);
            if (!request) {
                ++short_records;
                std::cout << "short\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\n";
                continue;
            }
            requested += request->count;
            const auto resource =
                game::resolve_object_spawn(*request, objects.entries(), items.entries());
            const auto* descriptor = resource ? objects.at(resource->descriptor_index) : nullptr;
            const bool ok = resource && resource->descriptor_index != 0 && descriptor != nullptr &&
                            descriptor->sprite_frame_index < frames.size() &&
                            frames.frames()[descriptor->sprite_frame_index].starts_group();
            resolved += ok ? 1 : 0;
            failures += ok ? 0 : 1;
            std::string_view status = "resolved";
            if (!resource || descriptor == nullptr)
                status = "missing_descriptor";
            else if (resource->descriptor_index == 0)
                status = "unused_descriptor";
            else if (!ok)
                status = "invalid_frame";
            std::cout << status << '\t' << request->object_id << '\t' << request->x << '\t'
                      << request->y << '\t' << request->z << '\t' << request->speed << '\t'
                      << static_cast<int>(request->count) << '\t' << request->scatter;
            if (!resource || descriptor == nullptr) {
                std::cout << "\t-\t-\t-\t-\t-\t-\n";
                continue;
            }
            std::cout << '\t' << resource->descriptor_index << '\t' << descriptor->flags << '\t'
                      << descriptor->lifetime << '\t' << descriptor->sprite_frame_index << '\t'
                      << (descriptor->sprite_frame_index < frames.size()
                              ? frames.frames()[descriptor->sprite_frame_index].group_length
                              : 0)
                      << '\t' << resource->item_id << '\n';
        }
    }
    std::cout << "SUMMARY\trecords\t" << records << "\tscripts\t" << scripts.size() << "\tevents\t"
              << events.size() << "\tshort\t" << short_records << "\tresolved\t" << resolved
              << "\trequested_objects\t" << requested << "\tfailures\t" << failures
              << "\truntime\tpartial_ids_1_1000_1050_2081_2100_4070_8080\n";
    return records > 0 && failures == 0 ? 0 : 1;
}

// Walk both CD2 events from entry with their default counter state. Exercise
// the effect and persistent loot together, without claiming player reachability.
int do_object_loot(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    data::MonsterStatsTable monsters;
    data::TextTable monster_text;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "CD2.blv", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None ||
        data::load_text_table(data_dir, "MONSTERS.TXT", monster_text) !=
            data::GameDataError::None ||
        data::MonsterStatsTable::parse(monster_text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: incomplete object-loot resources\n";
        return 1;
    }
    game::Battle battle;
    battle.reset(session, monsters, 1);
    std::size_t records = 0;
    std::size_t failures = 0;
    for (const auto& step : session.script.steps()) {
        const auto request = world::parse_object_spawn(step);
        if (!request || request->object_id != 1)
            continue;
        ++records;
        game::WalkState state;
        const auto outcome =
            game::walk_event(session.script, step.event_id, state, -1, session.file_name);
        game::ScriptLootState loot;
        game::ScriptObjectEffects effects;
        bool ok = outcome.unsupported.empty() && outcome.object_spawns.size() == 2;
        if (outcome.object_spawns.size() == 2) {
            ok = outcome.object_spawns.front().request.object_id == 2081 &&
                 outcome.object_spawns.back().request.object_id == 1 && ok;
        }
        for (const auto& spawn : outcome.object_spawns) {
            const auto made = effects.spawn(spawn.request, session, items, loot);
            ok = ok && made.error == game::LootSpawnError::None &&
                 made.created == spawn.request.count && made.dropped == 0;
        }
        if (loot.objects.empty()) {
            ++failures;
            continue;
        }
        const auto initial = effects.sprites(session.sprite_frames);
        ok = initial.size() == 1 && effects.active_count() == 1 && ok;
        (void)effects.advance(0, session);
        bool moved = false;
        std::size_t drawable = 0;
        std::size_t expired = 0;
        for (std::uint32_t tick = 1; tick <= 48; ++tick) {
            const auto advanced = effects.advance(1.0 / game::kObjectTicksPerSecond, session);
            game::advance_script_loot(loot, 1.0 / game::kObjectTicksPerSecond, session);
            expired += advanced.expired;
            ok = advanced.detonations.empty() && ok;
            const auto sprites = effects.sprites(session.sprite_frames);
            ok = sprites.size() == (tick < 48 ? 1U : 0U) && ok;
            for (const auto& sprite : sprites) {
                ok = sprite.animation_ticks == tick / 8 && ok;
                const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                      sprite.animation_ticks.value_or(0));
                if (!pick.entry.empty() && pick.scale > 0 &&
                    !cache.sprite(pick.entry, pick.palette).empty())
                    ++drawable;
                if (!initial.empty())
                    moved = moved || render::length(sprite.position - initial.front().position) > 0;
            }
        }
        ok = moved && expired == 1 && drawable == 47 && effects.active_count() == 0 && ok;
        const auto at = loot.objects.front().position + render::Vec3{0, 32, 0};
        std::array<game::Pack, 4> packs;
        for (auto& pack : packs)
            ok = pack.add(1, game::kPackWidth, game::kPackHeight) && ok;
        ok = game::take_script_loot(loot, session, items, cache, at, packs).empty() &&
             loot.objects.size() == request->count && ok;
        const auto memory = game::capture_map_memory(session, battle, {}, 1, loot);
        game::SaveState saved;
        saved.map_file = session.file_name;
        saved.remembered = game::save_map_memories({}, session.file_name, memory);
        game::SaveState loaded;
        ok = game::parse_save(game::save_text(saved), loaded) && ok;
        auto memories = game::load_map_memories(loaded.remembered);
        const auto key = game::map_memory_key(session.file_name);
        if (!memories.contains(key)) {
            ++failures;
            continue;
        }
        game::ScriptLootState restored;
        std::set<int> chests;
        ok = game::restore_map_memory(memories.at(key), game::MapMemoryUse::SavedSnapshot, 100,
                                      session, battle, chests, {},
                                      &restored) == game::MapMemoryResult::Restored &&
             ok;
        ok = game::valid_script_loot(restored, session, items) && ok;
        packs.front().clear();
        ok = game::take_script_loot(restored, session, items, cache, at, packs).size() ==
                 request->count &&
             restored.objects.empty() && ok;
        saved.remembered =
            game::save_map_memories(memories, session.file_name,
                                    game::capture_map_memory(session, battle, chests, 1, restored));
        ok = game::parse_save(game::save_text(saved), loaded) && ok;
        memories = game::load_map_memories(loaded.remembered);
        ok = memories.contains(key) && memories.at(key).loot.objects.empty() &&
             packs.front().size() == request->count && ok;
        // Re-entry with the same counter repeats both spawns; the counter's
        // other branch must not accidentally apply the earlier requests.
        const auto repeated =
            game::walk_event(session.script, step.event_id, state, -1, session.file_name);
        ok = repeated.object_spawns.size() == 2 && ok;
        for (const auto& spawn : repeated.object_spawns) {
            const auto made = effects.spawn(spawn.request, session, items, restored);
            ok = made.error == game::LootSpawnError::None && made.created == 1 && ok;
        }
        ok = effects.active_count() == 1 && restored.objects.size() == 1 && ok;
        effects.clear();
        ok = effects.active_count() == 0 && restored.objects.size() == 1 && ok;
        state.variables[106] = 1;
        const auto guarded =
            game::walk_event(session.script, step.event_id, state, -1, session.file_name);
        ok = guarded.object_spawns.empty() && ok;
        failures += ok ? 0 : 1;
        std::cout << "OBJECT_LOOT " << (ok ? "PASS" : "FAIL") << " event=" << step.event_id
                  << " entry=0 effect=2081 expired=" << expired << " drawable_samples=" << drawable
                  << " loot=" << static_cast<int>(request->count) << '\n';
    }
    std::cout << "OBJECT_LOOT_SUMMARY records=" << records << " failures=" << failures << '\n';
    return records == 2 && failures == 0 ? 0 : 1;
}

// Exercise the same dispatch/application/presentation seam used by the SDL
// adapter. Seed each spawn branch; this is not a natural-reachability witness.
int do_object_lifecycle(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "D18.blv", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: incomplete object-lifecycle resources\n";
        return 1;
    }
    game::ScriptObjectEffects live;
    game::ScriptLootState loot;
    std::size_t records = 0;
    std::size_t requested = 0;
    std::size_t created = 0;
    std::size_t failures = 0;
    for (const auto& step : session.script.steps()) {
        if (step.event_id != 56 || step.opcode != world::kOpcodeSpawnObjects)
            continue;
        ++records;
        const auto request = world::parse_object_spawn(step);
        if (!request) {
            ++failures;
            continue;
        }
        requested += request->count;
        game::WalkState state;
        const auto outcome = game::walk_event(session.script, step.event_id, state, step.sequence,
                                              session.file_name);
        if (outcome.object_spawns.size() != 1)
            ++failures;
        for (const auto& spawn : outcome.object_spawns) {
            const auto result = live.spawn(spawn.request, session, items, loot);
            created += result.created;
            if (result.error != game::LootSpawnError::None || result.dropped != 0)
                ++failures;
        }
    }
    const auto initial = live.sprites(session.sprite_frames);
    (void)live.advance(0, session);
    const auto paused = live.sprites(session.sprite_frames);
    if (paused.size() != created || paused.size() != initial.size())
        ++failures;
    std::size_t removed = 0;
    std::size_t detonations = 0;
    std::size_t bounces = 0;
    std::size_t drawable = 0;
    std::size_t zero_scale = 0;
    bool moved = false;
    std::uint32_t ticks = 0;
    constexpr std::uint32_t kMaximumTicks = 65536;
    for (; live.active_count() > 0 && ticks < kMaximumTicks; ++ticks) {
        const auto result = live.advance(1.0 / game::kObjectTicksPerSecond, session);
        removed += result.expired;
        detonations += result.detonations.size();
        bounces += result.bounces;
        const auto sprites = live.sprites(session.sprite_frames);
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            const auto& sprite = sprites[i];
            const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                  sprite.animation_ticks.value_or(0));
            // Installed ID 1000 selects DSFT's zero-scale null frame. It has
            // no billboard; particle trails remain a separate runtime gap.
            if (pick.scale == 0)
                ++zero_scale;
            else if (pick.entry.empty() || cache.sprite(pick.entry, pick.palette).empty())
                ++failures;
            else
                ++drawable;
            if (ticks == 0 && i < initial.size())
                moved = moved || render::length(sprite.position - initial[i].position) > 0;
        }
    }
    live.clear();
    const bool passed = records == 6 && failures == 0 && requested == created &&
                        removed == created && live.active_count() == 0 && moved &&
                        detonations > 0 && bounces > 0 && drawable > 0 && loot.objects.empty();
    std::cout << "OBJECT_LIFECYCLE " << (passed ? "PASS" : "FAIL") << " records=" << records
              << " requested=" << requested << " created=" << created << " removed=" << removed
              << " detonations=" << detonations << " bounces=" << bounces
              << " active=" << live.active_count() << " ticks=" << ticks << " failures=" << failures
              << " drawable_samples=" << drawable << " zero_scale_samples=" << zero_scale
              << " dispatch=seeded_branches\n";
    return passed ? 0 : 1;
}

// Walk the D01 event, then apply its object requests. Chest and monster-summon
// outcomes are outside this object-lifecycle probe; no actor-contact claim.
int do_object_impact(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "D01.blv", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: incomplete object-impact resources\n";
        return 1;
    }
    const auto& descriptors = session.object_descriptors.entries();
    const auto flight = std::ranges::find(descriptors, 2100, &world::ObjectDescriptor::object_id);
    const auto impact = std::ranges::find(descriptors, 2101, &world::ObjectDescriptor::object_id);
    if (flight == descriptors.end() || impact == descriptors.end() ||
        impact->sprite_frame_index >= session.sprite_frames.size()) {
        std::cerr << "error: missing object-impact definitions\n";
        return 1;
    }
    const auto& impact_group =
        session.sprite_frames.frames()[impact->sprite_frame_index].group_name;
    game::WalkState state;
    const auto outcome = game::walk_event(session.script, 47, state, -1, session.file_name);
    bool ok = outcome.unsupported.empty() && outcome.object_spawns.size() == 1;
    game::ScriptObjectEffects live;
    game::ScriptLootState loot;
    std::size_t created = 0;
    for (const auto& spawn : outcome.object_spawns) {
        const auto result = live.spawn(spawn.request, session, items, loot);
        created += result.created;
        ok = spawn.request.object_id == 2100 && result.error == game::LootSpawnError::None &&
             result.dropped == 0 && ok;
    }
    ok = created == 3 && live.active_count() == 3 && ok;
    // The event sets its one-time counter before requesting the objects.
    ok = state.variables[124] == 1 &&
         game::walk_event(session.script, 47, state, -1, session.file_name).object_spawns.empty() &&
         ok;
    std::size_t detonations = 0;
    std::size_t expired = 0;
    std::size_t visible_impacts = 0;
    std::uint32_t ticks = 0;
    bool impact_frame_zero = false;
    for (; live.active_count() > 0 && ticks < 65536; ++ticks) {
        const auto step = live.advance(1.0 / game::kObjectTicksPerSecond, session);
        detonations += step.detonations.size();
        expired += step.expired;
        for (const auto& detonation : step.detonations)
            ok = detonation.radius == 512 && ok;
        for (const auto& sprite : live.sprites(session.sprite_frames)) {
            const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                  sprite.animation_ticks.value_or(0));
            ok = !pick.entry.empty() && pick.scale > 0 &&
                 !cache.sprite(pick.entry, pick.palette).empty() && ok;
            if (sprite.animation == impact_group) {
                ++visible_impacts;
                impact_frame_zero = impact_frame_zero || sprite.animation_ticks == 0;
            }
        }
    }
    ok = detonations == 3 && expired == 3 && live.active_count() == 0 && visible_impacts > 0 &&
         impact_frame_zero && loot.objects.empty() && ok;
    std::cout << "OBJECT_IMPACT " << (ok ? "PASS" : "FAIL")
              << " event=47 object=2100 replacement=2101 created=" << created
              << " impacts=" << detonations << " expired=" << expired
              << " impact_samples=" << visible_impacts << " ticks=" << ticks
              << " lifetime=" << flight->lifetime << " replacement_lifetime=" << impact->lifetime
              << " replacement_flags=" << impact->flags
              << " replacement_frame=" << impact->sprite_frame_index
              << " descriptor=" << (flight - descriptors.begin())
              << " replacement_descriptor=" << (impact - descriptors.begin())
              << " actor_contacts=not_checked\n";
    return ok ? 0 : 1;
}

// Seed the three ID-4070 requests after the unimplemented opcode 3 and the
// preceding ID-1050 launches. This is a timed-effects witness, not the whole
// event or character-contact acceptance. Compare the terrain path with a
// model-only control to require an observable installed-data terrain response.
int do_object_expiry(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "OUTE3.odm", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: incomplete object-expiry resources\n";
        return 1;
    }
    const auto& descriptors = session.object_descriptors.entries();
    const auto flight = std::ranges::find(descriptors, 4070, &world::ObjectDescriptor::object_id);
    const auto impact = std::ranges::find(descriptors, 4071, &world::ObjectDescriptor::object_id);
    if (flight == descriptors.end() || impact == descriptors.end() ||
        impact->sprite_frame_index >= session.sprite_frames.size()) {
        std::cerr << "error: missing object-expiry definitions\n";
        return 1;
    }
    const auto& impact_group =
        session.sprite_frames.frames()[impact->sprite_frame_index].group_name;
    game::WalkState state;
    const auto outcome = game::walk_event(session.script, 220, state, 4, session.file_name);
    bool ok = outcome.unsupported.empty() && outcome.object_spawns.size() == 3;
    game::ScriptObjectEffects live;
    game::ScriptLootState loot;
    game::ScriptObjectEffects without_terrain;
    game::ScriptLootState control_loot;
    world::MapSession models_only;
    models_only.kind = world::MapKind::Indoor;
    models_only.collision = session.collision;
    std::size_t created = 0;
    for (const auto& spawn : outcome.object_spawns) {
        const auto result = live.spawn(spawn.request, session, items, loot);
        created += result.created;
        const auto control = without_terrain.spawn(spawn.request, session, items, control_loot);
        ok = control.created == result.created && control.error == result.error && ok;
        ok = spawn.request.object_id == 4070 && result.error == game::LootSpawnError::None &&
             result.dropped == 0 && ok;
    }
    ok = created == 45 && live.active_count() == 45 && ok;
    std::size_t detonations = 0;
    std::size_t expired = 0;
    std::size_t visible_impacts = 0;
    std::size_t contacts = 0;
    std::size_t terrain_contacts = 0;
    std::size_t changed_positions = 0;
    std::uint32_t ticks = 0;
    for (; live.active_count() > 0 && ticks < 65536; ++ticks) {
        const auto step = live.advance(1.0 / game::kObjectTicksPerSecond, session);
        detonations += step.detonations.size();
        expired += step.expired;
        contacts += step.bounces;
        terrain_contacts += step.terrain_contacts;
        (void)without_terrain.advance(1.0 / game::kObjectTicksPerSecond, models_only);
        const auto sprites = live.sprites(session.sprite_frames);
        const auto control = without_terrain.sprites(session.sprite_frames);
        ok = sprites.size() == control.size() && ok;
        for (std::size_t i = 0; i < std::min(sprites.size(), control.size()); ++i)
            changed_positions +=
                render::length(sprites[i].position - control[i].position) > 0.01f ? 1 : 0;
        if (!step.detonations.empty())
            ok = ticks + 1 == flight->lifetime && step.detonations.size() == created && ok;
        if (step.expired != 0)
            ok = ticks + 1 == flight->lifetime + impact->lifetime && ok;
        for (const auto& detonation : step.detonations)
            ok = detonation.radius == 512 && ok;
        for (const auto& sprite : sprites) {
            const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                  sprite.animation_ticks.value_or(0));
            ok = !pick.entry.empty() && pick.scale > 0 &&
                 !cache.sprite(pick.entry, pick.palette).empty() && ok;
            if (sprite.animation == impact_group) {
                ++visible_impacts;
                ok = ticks + 1 >= flight->lifetime &&
                     sprite.animation_ticks == (ticks + 1 - flight->lifetime) / 8 && ok;
            }
        }
    }
    ok = detonations == created && expired == created && live.active_count() == 0 &&
         visible_impacts == created * impact->lifetime && loot.objects.empty() &&
         terrain_contacts > 0 && changed_positions > 0 && ok;
    std::cout << "OBJECT_EXPIRY " << (ok ? "PASS" : "FAIL")
              << " event=220 entry=4 object=4070 replacement=4071 created=" << created
              << " transitions=" << detonations << " expired=" << expired
              << " impact_samples=" << visible_impacts << " ticks=" << ticks
              << " geometry_responses=" << contacts << " lifetime=" << flight->lifetime
              << " replacement_lifetime=" << impact->lifetime
              << " replacement_flags=" << impact->flags
              << " replacement_frame=" << impact->sprite_frame_index
              << " descriptor=" << (flight - descriptors.begin())
              << " replacement_descriptor=" << (impact - descriptors.begin())
              << " terrain_contacts=" << terrain_contacts
              << " changed_positions=" << changed_positions << " actor_contacts=not_checked\n";
    return ok ? 0 : 1;
}

// Skip the unsupported timer record, seed the activation counter, and apply
// only the object requests. Companion summons and actor contacts are excluded.
int do_object_contacts(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "OUTE3.odm", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None ||
        data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: incomplete object-contacts resources\n";
        return 1;
    }
    const auto target = std::ranges::find_if(monsters.entries(),
                                             [](const auto& row) { return row.hit_points > 0; });
    const auto& descriptors = session.object_descriptors.entries();
    const auto replacement =
        std::ranges::find(descriptors, 4071, &world::ObjectDescriptor::object_id);
    game::WalkState state;
    const auto outcome = game::walk_event(session.script, 220, state, 4, session.file_name);
    if (target == monsters.entries().end() || replacement == descriptors.end() ||
        replacement->sprite_frame_index >= session.sprite_frames.size() ||
        outcome.object_spawns.size() != 3 || !outcome.unsupported.empty()) {
        std::cerr << "error: missing object-contacts joins\n";
        return 1;
    }
    const auto& group = session.sprite_frames.frames()[replacement->sprite_frame_index].group_name;
    session.kind = world::MapKind::Indoor;
    session.collision = {};
    bool ok = true;
    std::size_t actor_contacts = 0;
    std::size_t party_contacts = 0;
    std::size_t removed = 0;
    std::size_t drawable = 0;
    for (const bool party_target : {false, true}) {
        for (const auto& spawn : outcome.object_spawns) {
            const auto& request = spawn.request;
            const render::Vec3 feet{
                static_cast<float>(request.x),
                static_cast<float>(request.z),
                static_cast<float>(request.y),
            };
            session.actors.clear();
            if (!party_target)
                session.actors.push_back(
                    {{}, {}, static_cast<int>(target - monsters.entries().begin() + 1), feet});
            game::Battle battle;
            battle.reset(session, monsters, 1);
            battle.hold_slot(0, 0, 100);
            const auto health = battle.health_of(0);
            const auto eye = party_target
                                 ? std::optional{feet + render::Vec3{0, game::kEyeHeight, 0}}
                                 : std::nullopt;
            game::ScriptLootState loot;
            game::ScriptObjectEffects live;
            const auto created = live.spawn(request, session, items, loot);
            const auto random = loot.random;
            ok = request.object_id == 4070 && created.error == game::LootSpawnError::None &&
                 created.created == 15 && created.dropped == 0 && ok;
            const auto hit = live.advance(1.0 / game::kObjectTicksPerSecond, session, battle,
                                          monsters, loot, eye);
            actor_contacts += hit.actor_contacts;
            party_contacts += hit.party_contacts;
            ok = hit.actor_contacts == (party_target ? 0U : created.created) &&
                 hit.party_contacts == (party_target ? created.created : 0U) &&
                 hit.detonations.size() == created.created && hit.actor_accepted == 0 &&
                 hit.expired == 0 && battle.health_of(0) == health &&
                 (party_target || battle.slot_up(0, 0)) && ok;
            for (const auto& detonation : hit.detonations)
                ok = detonation.radius == 512 && ok;
            const auto initial = live.sprites(session.sprite_frames);
            for (std::uint32_t age = 0; age < replacement->lifetime; ++age) {
                const auto sprites = live.sprites(session.sprite_frames);
                ok = sprites.size() == created.created && ok;
                for (std::size_t i = 0; i < sprites.size(); ++i) {
                    const auto& sprite = sprites[i];
                    const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                          sprite.animation_ticks.value_or(0));
                    ok = i < initial.size() && sprite.animation == group &&
                         sprite.animation_ticks == age / 8 &&
                         render::length(sprite.position - initial[i].position) == 0 &&
                         !pick.entry.empty() && pick.scale > 0 &&
                         !cache.sprite(pick.entry, pick.palette).empty() && ok;
                    ++drawable;
                }
                const auto step = live.advance(1.0 / game::kObjectTicksPerSecond, session, battle,
                                               monsters, loot, eye);
                removed += step.expired;
                ok = step.actor_contacts == 0 && step.party_contacts == 0 &&
                     step.detonations.empty() &&
                     step.expired == (age + 1 == replacement->lifetime ? created.created : 0U) &&
                     ok;
            }
            ok = live.active_count() == 0 && loot.random == random && ok;
        }
    }
    ok = actor_contacts == 45 && party_contacts == 45 && removed == 90 && ok;
    std::cout << "OBJECT_CONTACTS " << (ok ? "PASS" : "FAIL")
              << " object=4070 replacement=4071 actor_contacts=" << actor_contacts
              << " party_contacts=" << party_contacts << " removed=" << removed
              << " drawable_samples=" << drawable << " lifetime=" << replacement->lifetime
              << " controlled_overlap=1 map_geometry=excluded earlier_records=excluded\n";
    return ok ? 0 : 1;
}

int do_object_actor(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "OUTD3.odm", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None ||
        data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        std::cerr << "error: incomplete object-actor resources\n";
        return 1;
    }
    const auto vulnerable = std::ranges::find_if(monsters.entries(), [](const auto& row) {
        const int resistance = row.resistance(data::Resistance::Magic);
        return row.hit_points > 0 && row.level > 0 && resistance >= 0 && resistance < 200;
    });
    const auto immune = std::ranges::find_if(monsters.entries(), [](const auto& row) {
        const int resistance = row.resistance(data::Resistance::Magic);
        return row.hit_points > 0 && (resistance == data::kResistanceImmune || resistance >= 200);
    });
    const auto& descriptors = session.object_descriptors.entries();
    const auto replacement =
        std::ranges::find(descriptors, 8081, &world::ObjectDescriptor::object_id);
    game::WalkState state;
    state.variables[105] = 1;
    const auto outcome = game::walk_event(session.script, 200, state, 1, session.file_name);
    if (outcome.object_spawns.empty() || vulnerable == monsters.entries().end() ||
        immune == monsters.entries().end() || replacement == descriptors.end() ||
        replacement->sprite_frame_index >= session.sprite_frames.size()) {
        std::cerr << "error: missing object-actor joins\n";
        return 1;
    }
    const auto request = outcome.object_spawns.front().request;
    const auto& group = session.sprite_frames.frames()[replacement->sprite_frame_index].group_name;
    // Controlled initial overlap isolates contact semantics from map traversal.
    // Original timer activation, companion summons and sector selection are not tested.
    session.kind = world::MapKind::Indoor;
    session.collision = {};
    session.actors.clear();
    session.actors.push_back({
        {},
        {},
        0,
        {
            static_cast<float>(request.x),
            static_cast<float>(request.z),
            static_cast<float>(request.y),
        },
    });
    bool ok = request.object_id == 8080 && request.count == 1;
    std::size_t drawable = 0;
    for (int mode = 0; mode < 3; ++mode) {
        const bool accepted = mode == 0;
        const auto row = mode == 2 ? immune : vulnerable;
        session.actors.front().monster_id = static_cast<int>(row - monsters.entries().begin() + 1);
        game::Battle battle;
        battle.reset(session, monsters, 1);
        battle.hold_slot(0, 0, 100);
        const auto health = battle.health_of(0);
        std::uint32_t seed = 1;
        if (mode != 2) {
            const auto span = static_cast<unsigned>(std::clamp(row->level, 0, 255) +
                                                    row->resistance(data::Resistance::Magic) + 30);
            for (; seed < 65536; ++seed) {
                Mm6Random candidate{seed};
                if (request.scatter) {
                    (void)candidate.next();
                    (void)candidate.next();
                }
                if ((candidate.next() % span < 30) == accepted)
                    break;
            }
        }
        game::ScriptLootState loot;
        loot.random = seed;
        game::ScriptObjectEffects live;
        ok = seed < 65536 && live.spawn(request, session, items, loot).created == 1 && ok;
        Mm6Random expected{loot.random};
        if (mode != 2)
            (void)expected.next();
        const auto step =
            live.advance(1.0 / game::kObjectTicksPerSecond, session, battle, monsters, loot);
        ok = step.actor_contacts == 1 && step.actor_accepted == (accepted ? 1U : 0U) &&
             step.expired == (accepted ? 0U : 1U) && step.detonations.empty() &&
             step.missing_actor_replacements == 0 && loot.random == expected.state() &&
             battle.health_of(0) == health && battle.slot_up(0, 0) && ok;
        if (accepted) {
            render::Vec3 position{};
            for (std::uint32_t age = 0; age < replacement->lifetime; ++age) {
                const auto sprites = live.sprites(session.sprite_frames);
                if (sprites.size() != 1) {
                    ok = false;
                    break;
                }
                const auto& sprite = sprites.front();
                if (age == 0)
                    position = sprite.position;
                const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                      sprite.animation_ticks.value_or(0));
                ok = sprite.animation == group && sprite.animation_ticks == age / 8 &&
                     render::length(sprite.position - position) == 0 && !pick.entry.empty() &&
                     pick.scale > 0 && !cache.sprite(pick.entry, pick.palette).empty() && ok;
                ++drawable;
                const auto frame = live.advance(1.0 / game::kObjectTicksPerSecond, session, battle,
                                                monsters, loot);
                ok = frame.actor_contacts == 0 && frame.detonations.empty() &&
                     frame.expired == (age + 1 == replacement->lifetime ? 1U : 0U) && ok;
            }
        }
        ok = live.active_count() == 0 && loot.random == expected.state() && ok;
    }
    std::cout << "OBJECT_ACTOR " << (ok ? "PASS" : "FAIL")
              << " object=8080 replacement=8081 cases=accepted,resisted,immune"
              << " replacement_lifetime=" << replacement->lifetime
              << " drawable_samples=" << drawable
              << " controlled_overlap=1 map_geometry=excluded timer_and_summons=excluded\n";
    return ok ? 0 : 1;
}

int do_object_removal(const std::filesystem::path& data_dir) {
    using namespace starhaven;
    assets::AssetCache cache;
    cache.open(data_dir);
    world::MapSession session;
    data::ItemStatsTable items;
    if (world::load_map_session(data_dir / "Games.lod", data_dir, "OUTD3.odm", cache, session) !=
            world::MapSessionError::None ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None) {
        std::cerr << "error: incomplete object-removal resources\n";
        return 1;
    }
    const auto& descriptors = session.object_descriptors.entries();
    const auto flight = std::ranges::find(descriptors, 8080, &world::ObjectDescriptor::object_id);
    if (flight == descriptors.end() || flight->sprite_frame_index >= session.sprite_frames.size()) {
        std::cerr << "error: missing object-removal definition\n";
        return 1;
    }
    const auto& group = session.sprite_frames.frames()[flight->sprite_frame_index].group_name;
    game::WalkState state;
    bool ok =
        game::walk_event(session.script, 200, state, 1, session.file_name).object_spawns.empty();
    state.variables[105] = 1;
    const auto outcome = game::walk_event(session.script, 200, state, 1, session.file_name);
    ok = outcome.unsupported.empty() && outcome.object_spawns.size() == 3 &&
         state.variables[106] == 1 && ok;
    state.variables[106] = 8;
    ok = game::walk_event(session.script, 200, state, 1, session.file_name).object_spawns.empty() &&
         ok;
    game::ScriptObjectEffects live;
    game::ScriptObjectEffects airborne;
    game::ScriptLootState loot;
    game::ScriptLootState air_loot;
    const world::MapSession empty;
    std::size_t created = 0;
    for (const auto& spawn : outcome.object_spawns) {
        const auto result = live.spawn(spawn.request, session, items, loot);
        const auto control = airborne.spawn(spawn.request, session, items, air_loot);
        created += result.created;
        ok = spawn.request.object_id == 8080 && result.error == game::LootSpawnError::None &&
             result.dropped == 0 && control.created == result.created &&
             control.error == result.error && ok;
    }
    std::size_t expired = 0;
    std::size_t early_removals = 0;
    std::size_t air_expired = 0;
    std::size_t drawable = 0;
    std::uint32_t ticks = 0;
    for (; (live.active_count() > 0 || airborne.active_count() > 0) && ticks < 65536; ++ticks) {
        for (const auto* effects : {&live, &airborne}) {
            for (const auto& sprite : effects->sprites(session.sprite_frames)) {
                const auto pick = game::choose_sprite(session.sprite_frames, sprite.animation,
                                                      sprite.animation_ticks.value_or(0));
                ok = sprite.animation == group && sprite.animation_ticks == ticks / 8 &&
                     !pick.entry.empty() && pick.scale > 0 &&
                     !cache.sprite(pick.entry, pick.palette).empty() && ok;
                ++drawable;
            }
        }
        const auto step = live.advance(1.0 / game::kObjectTicksPerSecond, session);
        const auto air_step = airborne.advance(1.0 / game::kObjectTicksPerSecond, empty);
        expired += step.expired;
        air_expired += air_step.expired;
        if (ticks + 1 < flight->lifetime)
            early_removals += step.expired;
        if (air_step.expired > 0)
            ok = ticks + 1 == flight->lifetime && ok;
        ok = step.detonations.empty() && air_step.detonations.empty() && ok;
    }
    ok = created == 3 && expired == created && air_expired == created && early_removals > 0 &&
         live.active_count() == 0 && airborne.active_count() == 0 && drawable > 0 &&
         loot.objects.empty() && air_loot.objects.empty() && ok;
    std::cout << "OBJECT_REMOVAL " << (ok ? "PASS" : "FAIL")
              << " event=200 entry=1 counter105=1 object=8080 created=" << created
              << " removed=" << expired << " geometry_removals=" << early_removals
              << " airborne_expired=" << air_expired << " ticks=" << ticks
              << " drawable_samples=" << drawable
              << " descriptor=" << (flight - descriptors.begin()) << " flags=" << flight->flags
              << " lifetime=" << flight->lifetime << " frame=" << flight->sprite_frame_index
              << " detonations=0 actor_contacts=not_checked summons=not_applied\n";
    return ok ? 0 : 1;
}

// Generate each opcode-41 reward in isolation, using user-owned tables and a
// fixed seed. The output contains numeric compatibility metadata only.
int do_generated_items(const starhaven::lod::LodArchive& icons,
                       const std::filesystem::path& data_dir) {
    using namespace starhaven;
    data::ItemStatsTable items;
    data::RandomItemTable random_items;
    data::StandardBonusTable standard;
    data::SpecialBonusTable special;
    if (!game::audit_script_coverage(icons).complete() ||
        data::load_item_stats(data_dir, items) != data::GameDataError::None ||
        data::load_random_items(data_dir, random_items) != data::GameDataError::None ||
        data::load_standard_bonuses(data_dir, standard) != data::GameDataError::None ||
        data::load_special_bonuses(data_dir, special) != data::GameDataError::None) {
        std::cerr << "error: incomplete script or item-generation input\n";
        return 1;
    }
    std::size_t records = 0;
    std::size_t short_records = 0;
    std::size_t generated = 0;
    std::size_t overrides = 0;
    std::size_t failures = 0;
    std::cout << "script\tevent\tsequence\tstatus\tlevel\ttype\toverride\titem"
                 "\tstandard\tstrength\tspecial\tcharges\tidentified\n";
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            ++failures;
            continue;
        }
        if (game::script_scope(entry.name) == "global.evt") {
            game::ScriptItemState rewards;
            game::ScriptItemGenerator generator{random_items, items, standard, special, rewards};
            game::WalkState state;
            const auto out =
                game::walk_event(script, 426, state, -1, entry.name, nullptr, &generator, 0);
            const bool ok = out.ran && out.failed_items.empty() &&
                            out.generated_items.size() == 1 && state.items.size() == 1 &&
                            state.variables[24] == 3 && out.unsupported.empty() &&
                            out.failed_decorations.empty() && out.decorations.size() == 1 &&
                            out.decorations.front().event == 424;
            const auto repeat =
                game::walk_event(script, 424, state, -1, entry.name, nullptr, &generator, 0);
            failures += repeat.generated_items.empty() && repeat.failed_items.empty() &&
                                repeat.unsupported.empty() && repeat.failed_decorations.empty()
                            ? 0
                            : 1;
            failures += ok ? 0 : 1;
            std::cout << "FLOW\t" << entry.name << "\t426\t" << (ok ? "pass" : "fail")
                      << "\trewards\t" << out.generated_items.size() << "\tnext_event\t424\n";
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeGenerateItem) {
                continue;
            }
            ++records;
            std::cout << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t';
            const auto request = world::parse_script_item(step);
            if (!request) {
                ++short_records;
                std::cout << "short\t-\t-\t-\t-\t-\t-\t-\t-\t-\n";
                continue;
            }
            game::ScriptItemState state;
            game::ScriptItemGenerator generator{random_items, items, standard, special, state};
            const auto item = generator.generate(*request);
            std::cout << (item ? "generated" : "failed") << '\t' << static_cast<int>(request->level)
                      << '\t' << static_cast<int>(request->type) << '\t' << request->item_id;
            if (!item || item->item_id <= 0 ||
                items.at(static_cast<std::size_t>(item->item_id)) == nullptr) {
                ++failures;
                std::cout << "\t-\t-\t-\t-\t-\t-\n";
                continue;
            }
            ++generated;
            overrides += request->item_id != 0 ? 1 : 0;
            std::cout << '\t' << item->item_id << '\t' << item->standard_bonus << '\t'
                      << item->standard_bonus_strength << '\t' << item->special_bonus << '\t'
                      << item->charges << '\t' << item->identified << '\n';
        }
    }
    std::cout << "SUMMARY\trecords\t" << records << "\tshort\t" << short_records << "\tgenerated\t"
              << generated << "\toverrides\t" << overrides << "\tfailures\t" << failures << '\n';
    return records > 0 && failures == 0 ? 0 : 1;
}

// Inspect every mask against its own map, then walk representative full effects.
int do_face_bits(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    using namespace starhaven;
    if (!game::audit_script_coverage(icons).complete())
        return 1;
    assets::AssetCache cache;
    cache.open(data_dir);
    std::size_t records = 0;
    std::size_t short_records = 0;
    std::size_t applied = 0;
    std::size_t failures = 0;
    std::size_t draw_path = 0;
    std::set<std::string> maps;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name))
            continue;
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None)
            return 1;
        world::MapSession session;
        bool loaded = false;
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeSetFaceBits)
                continue;
            ++records;
            const auto change = world::parse_face_bits(step);
            std::cout << "RECORD\t" << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t';
            if (!change) {
                ++short_records;
                std::cout << "short\n";
                continue;
            }
            if (!loaded) {
                const auto name = entry.name.substr(0, entry.name.size() - 4) + ".blv";
                if (world::load_map_session(data_dir / "Games.lod", data_dir, name, cache,
                                            session) != world::MapSessionError::None) {
                    std::cout << "map-fail\n";
                    ++failures;
                    continue;
                }
                loaded = true;
                maps.insert(game::script_scope(name));
            }
            if (change->index >= session.blv.faces.size()) {
                std::cout << "index-fail\n";
                ++failures;
                continue;
            }
            auto& face = session.blv.faces[change->index];
            const auto before = face.attributes;
            const auto expected = change->set ? before | change->mask : before & ~change->mask;
            game::WalkState state;
            const auto outcome = game::walk_event(script, step.event_id, state, step.sequence);
            game::FaceChanges memory;
            const bool ok =
                !outcome.faces.empty() && outcome.faces.front().index == change->index &&
                outcome.faces.front().mask == change->mask &&
                outcome.faces.front().set == change->set &&
                game::apply_script_faces(session, std::span(outcome.faces).first(1), memory) == 1 &&
                face.attributes == expected;
            applied += ok ? 1 : 0;
            failures += ok ? 0 : 1;
            draw_path += change->mask == 0x10U ? 1 : 0;
            std::cout << (ok ? "pass" : "fail") << '\t' << change->index << '\t' << change->mask
                      << '\t' << change->set << '\n';
            face.attributes = before;
        }
    }
    const auto flow = [&](std::string_view map, std::uint16_t event, int start,
                          std::span<const std::uint32_t> targets, bool animation) {
        world::MapSession session;
        if (world::load_map_session(data_dir / "Games.lod", data_dir, map, cache, session) !=
            world::MapSessionError::None)
            return false;
        const auto collision_before = session.collision.size();
        game::WalkState state;
        const auto outcome = game::walk_event(session.script, event, state, start);
        game::SaveState saved;
        saved.map_file = session.file_name;
        if (outcome.faces.empty() || !outcome.unsupported.empty() ||
            game::apply_script_faces(session, outcome.faces, saved.faces) != outcome.faces.size())
            return false;
        for (const auto target : targets) {
            if (target >= session.blv.faces.size())
                return false;
            const auto& face = session.blv.faces[target];
            if (animation) {
                const auto* loop =
                    world::find_texture_animation(session.texture_animations, face.texture_name);
                if ((face.attributes & world::kFaceTextureAnimated) == 0 || loop == nullptr ||
                    loop->frames.size() < 2 ||
                    world::texture_frame_name(
                        face.texture_name, session.texture_animations,
                        static_cast<std::uint32_t>(loop->frames.front().duration),
                        true) != loop->frames[1].name)
                    return false;
            } else if (!face.ethereal()) {
                return false;
            }
        }
        if (!animation && session.collision.size() >= collision_before)
            return false;
        game::SaveState loaded;
        if (!game::parse_save(game::save_text(saved), loaded))
            return false;
        world::MapSession reopened;
        if (world::load_map_session(data_dir / "Games.lod", data_dir, map, cache, reopened) !=
            world::MapSessionError::None)
            return false;
        game::restore_script_faces(reopened, loaded.faces);
        for (const auto target : targets) {
            if (reopened.blv.faces[target].attributes != session.blv.faces[target].attributes ||
                reopened.blv.faces[target].texture_name != session.blv.faces[target].texture_name)
                return false;
        }
        if (map == "D12.blv") {
            const auto stop = game::walk_event(reopened.script, 23, state);
            if (game::apply_script_faces(reopened, stop.faces, loaded.faces) != stop.faces.size() ||
                (reopened.blv.faces[targets.front()].attributes & world::kFaceTextureAnimated) != 0)
                return false;
        }
        return true;
    };
    const std::array<std::uint32_t, 2> passage{4522, 4575};
    const std::array<std::uint32_t, 1> painting{1420};
    const std::array<std::uint32_t, 1> portrait{5290};
    const auto report = [&](std::string_view label, bool ok) {
        std::cout << "FLOW\t" << label << '\t' << (ok ? "pass" : "fail") << '\n';
        failures += ok ? 0 : 1;
    };
    report("CD2/33 passage and reload", flow("CD2.blv", 33, 5, passage, false));
    report("D12/22 animation reload and stop", flow("D12.blv", 22, -1, painting, true));
    report("D17/55 animation and reload", flow("D17.blv", 55, -1, portrait, true));
    std::cout << "SUMMARY\trecords\t" << records << "\tshort\t" << short_records << "\tapplied\t"
              << applied << "\tmaps\t" << maps.size() << "\talternate_draw_path_records\t"
              << draw_path << "\tfailures\t" << failures << '\n';
    return records > 0 && failures == 0 ? 0 : 1;
}

// Exercise current-decoration writes and join initial interactions to GLOBAL.
int do_decoration_events(const starhaven::lod::LodArchive& icons,
                         const std::filesystem::path& data_dir) {
    using namespace starhaven;
    if (!game::audit_script_coverage(icons).complete())
        return 1;
    std::size_t records = 0;
    std::size_t short_records = 0;
    std::size_t failures = 0;
    std::size_t changed = 0;
    std::size_t hidden = 0;
    world::MapScript global;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name))
            continue;
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None)
            return 1;
        if (game::script_scope(entry.name) == "global.evt")
            global = script;
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeSetDecorationEvent)
                continue;
            ++records;
            const auto value = world::parse_decoration_event(step);
            std::cout << "RECORD\t" << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t';
            if (!value) {
                ++short_records;
                std::cout << "short\n";
                continue;
            }
            world::MapSession session;
            session.file_name = "Probe.blv";
            session.decorations.resize(1);
            session.decorations.front().descriptor_id = 167;
            session.decorations.front().event_value = 26;
            game::WalkState state;
            const auto out = game::walk_event(script, step.event_id, state, step.sequence,
                                              entry.name, nullptr, nullptr, 0);
            game::DecorationChanges memory;
            const auto count = game::apply_script_decorations(session, out.decorations, memory);
            const auto& decoration = session.decorations.front();
            const auto expected =
                *value == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>((*value + 112U) & 0xffU);
            const bool ok = count == 1 && out.failed_decorations.empty() &&
                            decoration.event_value == expected &&
                            decoration.active() == (*value != 0);
            failures += ok ? 0 : 1;
            hidden += *value == 0 ? 1 : 0;
            changed += *value != 0 ? 1 : 0;
            std::cout << (ok ? "pass" : "fail") << '\t' << *value << '\t'
                      << static_cast<int>(expected) << '\n';
        }
    }
    lod::GameLodArchive games;
    if (lod::GameLodArchive::open(data_dir / "Games.lod", games) != lod::GameLodError::None)
        return 1;
    assets::AssetCache cache;
    cache.open(data_dir);
    std::size_t maps = 0;
    std::size_t implicit = 0;
    std::size_t unresolved = 0;
    for (const auto& entry : games.entries()) {
        const auto name = game::script_scope(entry.name);
        if (!name.ends_with(".blv") && !name.ends_with(".odm"))
            continue;
        world::MapSession session;
        if (world::load_map_session(data_dir / "Games.lod", data_dir, entry.name, cache, session) !=
            world::MapSessionError::None) {
            ++failures;
            continue;
        }
        ++maps;
        game::initialize_decoration_events(session);
        std::size_t local_implicit = 0;
        std::size_t local_unresolved = 0;
        for (std::size_t i = 0; i < session.decorations.size(); ++i) {
            const auto interaction =
                game::decoration_interaction(session, static_cast<std::uint32_t>(i));
            if (!interaction || !interaction->global)
                continue;
            ++local_implicit;
            local_unresolved += global.defines(interaction->event) ? 0 : 1;
        }
        implicit += local_implicit;
        unresolved += local_unresolved;
        std::cout << "MAP\t" << entry.name << '\t' << local_implicit << '\t' << local_unresolved
                  << '\n';
    }
    std::cout << "SUMMARY\trecords\t" << records << "\tshort\t" << short_records << "\thidden\t"
              << hidden << "\tchanged\t" << changed << "\tmaps\t" << maps << "\tinteractions\t"
              << implicit << "\tunresolved\t" << unresolved << "\tfailures\t" << failures << '\n';
    return records != 0 && maps != 0 && failures == 0 && unresolved == 0 ? 0 : 1;
}

// Metadata-only joins and direct suspension probes. The preceding text is a
// syntactic candidate, not proof that a branch executes that text selection.
int do_messages(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    using namespace starhaven;
    data::NpcDialogueTable dialogue;
    if (data::load_npc_dialogue(data_dir, dialogue) != data::GameDataError::None) {
        std::cerr << "error: could not load NPC dialogue tables\n";
        return 1;
    }
    const auto coverage = game::audit_script_coverage(icons);
    if (!coverage.complete()) {
        std::cerr << "error: incomplete script input\n";
        return 1;
    }
    std::size_t records = 0;
    std::size_t empty = 0;
    std::size_t candidates = 0;
    std::size_t resolved = 0;
    std::size_t next_steps = 0;
    std::size_t failures = 0;
    std::cout << "script\tevent\tsequence\targument_size\ttext_candidate\ttext_resolves"
                 "\tnext_opcode\tsuspends\n";
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            ++failures;
            continue;
        }
        if (std::ranges::none_of(script.steps(), [](const auto& step) {
                return step.opcode == world::kOpcodeShowMessage;
            })) {
            continue;
        }
        const bool global = game::script_scope(entry.name) == "global.evt";
        world::MapStrings strings;
        const auto name = entry.name.substr(0, entry.name.size() - 4) + ".STR";
        const bool strings_ok =
            global || (icons.payload(name, raw) == lod::LodArchive::PayloadError::None &&
                       world::MapStrings::parse(raw, strings) == world::MapScriptError::None);
        // Full event probes supplement the isolated record census. Only state
        // counts and text indices are printed; no dialogue or argument bytes.
        int probe_event = 0;
        int expected_text = -1;
        if (global) {
            probe_event = 20;
            expected_text = 28;
        } else if (game::script_scope(entry.name) == "oute3.evt") {
            probe_event = 240;
            expected_text = 27;
        } else if (game::script_scope(entry.name) == "cd2.evt") {
            probe_event = 53;
            expected_text = 7;
        }
        if (probe_event != 0) {
            game::WalkState state;
            game::WalkPresentation text;
            const auto first = game::walk_event(script, static_cast<std::uint16_t>(probe_event),
                                                state, -1, entry.name, &text);
            std::cout << "FLOW\t" << entry.name << '\t' << probe_event << "\tbefore\t"
                      << (first.message ? first.message->text : -1) << '\t' << state.bits.size()
                      << '\t' << state.npc_topics.size() << '\t' << first.doors.size() << '\n';
            if (first.message) {
                const auto bits_before = state.bits;
                const auto topics_before = state.npc_topics;
                const bool before_ok = first.unsupported.empty() && first.doors.empty() &&
                                       first.message->text == expected_text &&
                                       bits_before.size() == (global ? 1 : 0) &&
                                       topics_before.size() == (global ? 1 : 0);
                const auto last =
                    game::walk_event(script, static_cast<std::uint16_t>(probe_event), state,
                                     first.message->resume_at, entry.name, &text);
                const bool after_ok = last.ran && !last.message && last.unsupported.empty() &&
                                      state.npc_topics == topics_before &&
                                      (!global || state.bits == bits_before) &&
                                      state.bits.size() == (probe_event == 53 ? 0 : 1) &&
                                      last.doors.size() == (probe_event == 53 ? 1 : 0);
                failures += before_ok && after_ok ? 0 : 1;
                std::cout << "FLOW\t" << entry.name << '\t' << probe_event << "\tafter\t"
                          << (last.message ? last.message->text : -1) << '\t' << state.bits.size()
                          << '\t' << state.npc_topics.size() << '\t' << last.doors.size() << '\n';
            } else {
                ++failures;
            }
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeShowMessage) {
                continue;
            }
            ++records;
            empty += step.arguments.empty() ? 1 : 0;
            int candidate = -1;
            int next_opcode = -1;
            for (const auto& other : script.event(step.event_id)) {
                if (static_cast<int>(other.sequence) == static_cast<int>(step.sequence) - 1 &&
                    (other.opcode == world::kOpcodeLongMessage ||
                     (global && other.opcode == world::kOpcodeMessage)) &&
                    !other.arguments.empty()) {
                    std::uint32_t value = 0;
                    for (std::size_t i = std::min<std::size_t>(4, other.arguments.size());
                         i-- > 0;) {
                        value = (value << 8U) | static_cast<std::uint32_t>(other.arguments[i]);
                    }
                    candidate = static_cast<int>(value);
                }
                if (static_cast<int>(other.sequence) == static_cast<int>(step.sequence) + 1 &&
                    other.opcode != world::kOpcodeHeader) {
                    next_opcode = other.opcode;
                }
            }
            candidates += candidate >= 0 ? 1 : 0;
            const auto* npc = global ? dialogue.at(candidate) : nullptr;
            const bool resolves =
                strings_ok && candidate >= 0 &&
                (global ? npc != nullptr && !npc->text.empty()
                        : !strings.at(static_cast<std::size_t>(candidate)).empty());
            resolved += resolves ? 1 : 0;
            next_steps += next_opcode >= 0 ? 1 : 0;
            game::WalkState state;
            game::WalkPresentation presentation{candidate};
            const auto out = game::walk_event(script, step.event_id, state, step.sequence,
                                              entry.name, &presentation);
            const bool suspends = out.message && out.message->text == candidate &&
                                  out.message->resume_at == static_cast<int>(step.sequence) + 1 &&
                                  out.said.empty() && out.given.empty() && out.taken.empty() &&
                                  out.unsupported.empty();
            failures += !suspends || (candidate >= 0 && !resolves) ? 1 : 0;
            std::cout << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t' << step.arguments.size() << '\t'
                      << candidate << '\t' << resolves << '\t' << next_opcode << '\t' << suspends
                      << '\n';
        }
    }
    std::cout << "SUMMARY\trecords\t" << records << "\tempty_arguments\t" << empty
              << "\tpreceding_text\t" << candidates << "\tresolved_text\t" << resolved
              << "\tnext_steps\t" << next_steps << "\tfailures\t" << failures << '\n';
    return records > 0 && failures == 0 ? 0 : 1;
}

// Read-only install check: apply decoration changes to disposable map sessions,
// never walk whole events or write saves. Only numeric compatibility metadata.
int do_decorations(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    using namespace starhaven;
    lod::GameLodArchive games;
    if (lod::GameLodArchive::open(data_dir / "Games.lod", games) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    assets::AssetCache cache;
    cache.open(data_dir);
    std::size_t records = 0;
    std::size_t decoded = 0;
    std::size_t applied = 0;
    std::size_t invalid_targets = 0;
    std::size_t unmapped = 0;
    std::size_t failures = 0;
    std::size_t unknown_names = 0;
    std::size_t missing_art = 0;
    std::cout << "script\tevent\tsequence\tstatus\tindex\tvisible\tkeep_descriptor\n";
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            ++failures;
            std::cerr << "error: could not parse " << entry.name << '\n';
            continue;
        }
        if (std::ranges::none_of(script.steps(), [](const auto& step) {
                return step.opcode == world::kOpcodeSetDecoration;
            })) {
            continue;
        }
        const auto stem = entry.name.substr(0, entry.name.size() - 4);
        auto map = games.find(stem + ".blv");
        if (!map) {
            map = games.find(stem + ".odm");
        }
        world::MapSession session;
        if (map && world::load_map_session(data_dir / "Games.lod", data_dir, map->name, cache,
                                           session) != world::MapSessionError::None) {
            ++failures;
            std::cerr << "error: could not load " << map->name << '\n';
            continue;
        }
        game::DecorationChanges memory;
        for (const auto& step : script.steps()) {
            if (step.opcode != world::kOpcodeSetDecoration) {
                continue;
            }
            ++records;
            std::cout << entry.name << '\t' << step.event_id << '\t'
                      << static_cast<int>(step.sequence) << '\t';
            const auto change = world::parse_decoration_change(step);
            if (!change) {
                std::cout << "invalid_record\t-\t-\t-\n";
                continue;
            }
            ++decoded;
            if (!map) {
                ++unmapped;
                std::cout << "no_map";
            } else if (change->index >= session.decorations.size()) {
                ++invalid_targets;
                std::cout << "invalid_index";
            } else {
                if (change->name != "0" && session.decoration_types.find(change->name) == nullptr) {
                    ++unknown_names;
                }
                const auto count =
                    game::apply_script_decorations(session, std::span(&*change, 1), memory);
                if (count != 1 || session.decorations[change->index].active() != change->visible) {
                    ++failures;
                    std::cout << "failed";
                } else {
                    ++applied;
                    std::cout << "applied";
                    const auto& decoration = session.decorations[change->index];
                    const auto& animation = session.decoration_animation(decoration);
                    const auto frames = session.sprite_frames.group(animation);
                    const auto sprite =
                        frames.empty() ? animation
                                       : world::SpriteFrameTable::sprite_entry(frames.front(), 0);
                    if (decoration.visible() && !cache.has_sprite(sprite)) {
                        ++missing_art;
                        const auto* type = session.decoration_types.at(decoration.descriptor_id);
                        const bool descriptor_art =
                            type != nullptr && type->sprite_id < session.sprite_frames.size() &&
                            cache.has_sprite(world::SpriteFrameTable::sprite_entry(
                                session.sprite_frames.frames()[type->sprite_id], 0));
                        std::cerr << "art gap: " << entry.name << " event=" << step.event_id
                                  << " sequence=" << static_cast<int>(step.sequence)
                                  << " descriptor=" << decoration.descriptor_id
                                  << " descriptor_art=" << descriptor_art << '\n';
                    }
                }
            }
            std::cout << '\t' << change->index << '\t' << change->visible << '\t'
                      << (change->name == "0") << '\n';
        }
    }
    std::cout << "SUMMARY records=" << records << " decoded=" << decoded << " applied=" << applied
              << " invalid_indices=" << invalid_targets << " no_map=" << unmapped
              << " unknown_names=" << unknown_names << " missing_art=" << missing_art
              << " failures=" << failures << '\n';
    return failures == 0 && records != 0 && missing_art == 0 ? 0 : 1;
}

// Research mode: what does each opcode look like across every script, and do
// any arguments carry a map file name? A transition has to name where it
// goes, and the 67 maps are a closed set to test against.
int do_scan(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    struct OpcodeShape {
        std::size_t uses = 0;
        std::size_t min_size = static_cast<std::size_t>(-1);
        std::size_t max_size = 0;
        std::size_t file_names = 0;  // arguments holding ".odm"/".blv"
        std::string example;         // one such name, with its map
    };
    std::map<int, OpcodeShape> shapes;
    std::size_t scripts = 0;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        ++scripts;
        for (const auto& step : script.steps()) {
            OpcodeShape& shape = shapes[step.opcode];
            ++shape.uses;
            shape.min_size = std::min(shape.min_size, step.arguments.size());
            shape.max_size = std::max(shape.max_size, step.arguments.size());

            // Case-insensitively look for ".odm" or ".blv" in the raw bytes.
            std::string lowered;
            lowered.reserve(step.arguments.size());
            for (const std::uint8_t b : step.arguments) {
                lowered += static_cast<char>(std::tolower(b));
            }
            const std::size_t at = std::min(lowered.find(".odm"), lowered.find(".blv"));
            if (at != std::string::npos) {
                ++shape.file_names;
                if (shape.example.empty()) {
                    // The name starts after the previous NUL, or at the front.
                    const std::size_t start = lowered.rfind('\0', at) + 1;
                    shape.example = name.substr(0, name.size() - 4) + " event " +
                                    std::to_string(step.event_id) + ": \"" +
                                    lowered.substr(start, at + 4 - start) + "\"";
                }
            }
        }
    }

    std::cout << scripts << " scripts\n";
    std::cout << "opcode\tuses\targ bytes\tfile names\n";
    for (const auto& [opcode, shape] : shapes) {
        std::cout << opcode << "\t" << shape.uses << "\t" << shape.min_size;
        if (shape.max_size != shape.min_size) {
            std::cout << ".." << shape.max_size;
        }
        std::cout << "\t" << shape.file_names;
        if (!shape.example.empty()) {
            std::cout << "\te.g. " << shape.example;
        }
        std::cout << "\n";
    }
    return 0;
}

// Research mode: every use of opcode 6 across every script, read as four
// little-endian i32s and a NUL-terminated destination, tested against the
// design table's own set of 67 map file names.
int do_transitions(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::MapStatsTable maps;
    if (data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
        std::cerr << "error: could not read MapStats.txt\n";
        return 1;
    }
    const auto lower = [](std::string_view text) {
        std::string out;
        for (const char c : text) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };
    std::map<std::string, std::string> known;  // lowered file name -> display name
    for (const auto& m : maps.entries()) {
        known[lower(m.file_name)] = m.name;
    }

    std::map<std::size_t, std::size_t> sizes;
    std::map<std::string, std::size_t> middle_patterns;  // bytes 16..25 as hex
    std::size_t uses = 0;
    std::size_t same_map = 0;
    std::size_t named = 0;
    std::size_t known_names = 0;
    std::map<std::string, std::size_t> destinations;
    std::map<std::string, std::size_t> unknown_names;
    std::int32_t min_coord[3] = {0, 0, 0};
    std::int32_t max_coord[3] = {0, 0, 0};
    std::int32_t max_facing = 0;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 6) {
                continue;
            }
            ++uses;
            ++sizes[step.arguments.size()];
            const auto& a = step.arguments;
            if (a.size() >= 26) {
                std::string middle;
                for (std::size_t i = 16; i < 26; ++i) {
                    char hex[4];
                    std::snprintf(hex, sizeof hex, "%02x ", a[i]);
                    middle += hex;
                }
                ++middle_patterns[middle];
            }
            if (a.size() >= 27 && a[26] == '0' && (a.size() == 28 ? a[27] == 0 : true)) {
                ++same_map;
            }
            if (a.size() >= 16) {
                for (int c = 0; c < 3; ++c) {
                    std::int32_t v = 0;
                    for (int i = 3; i >= 0; --i) {
                        v = (v << 8) | a[static_cast<std::size_t>(c * 4 + i)];
                    }
                    min_coord[c] = std::min(min_coord[c], v);
                    max_coord[c] = std::max(max_coord[c], v);
                }
                std::int32_t facing = 0;
                for (int i = 15; i >= 12; --i) {
                    facing = (facing << 8) | a[static_cast<std::size_t>(i)];
                }
                max_facing = std::max(max_facing, facing);
            }
            if (a.size() > 26) {
                std::string text;
                for (std::size_t i = 26; i < a.size() && a[i] != 0; ++i) {
                    text += static_cast<char>(std::tolower(a[i]));
                }
                if (!text.empty() && text != "0") {
                    ++named;
                    if (known.contains(text)) {
                        ++known_names;
                        ++destinations[name.substr(0, name.size() - 4) + " -> " + text];
                    } else {
                        ++unknown_names[name.substr(0, name.size() - 4) + " -> " + text];
                    }
                }
            }
        }
    }

    std::cout << uses << " uses of opcode 6\n";
    std::cout << "argument sizes:";
    for (const auto& [size, count] : sizes) {
        std::cout << "  " << size << "b x" << count;
    }
    std::cout << "\n";
    std::cout << same_map << " say \"0\" instead of a destination\n";
    std::cout << named << " name a destination; " << known_names
              << " are maps the design table lists, " << destinations.size() << " distinct\n";
    // The trailing pair 24..25 read as a little-endian u16, against the
    // sound table: consecutive runs like 665..675 are id-shaped.
    {
        world::SoundTable sounds;
        std::span<const std::byte> raw2;
        if (icons.payload("DSOUNDS.BIN", raw2) == lod::LodArchive::PayloadError::None &&
            world::SoundTable::parse(raw2, sounds) == world::SoundTableError::None) {
            std::map<int, std::size_t> tails;
            for (const auto& [pattern, count] : middle_patterns) {
                // The pattern is hex bytes "xx " * 10; the last two are 24, 25.
                const int b24 =
                    static_cast<int>(std::strtol(pattern.substr(24, 2).c_str(), nullptr, 16));
                const int b25 =
                    static_cast<int>(std::strtol(pattern.substr(27, 2).c_str(), nullptr, 16));
                tails[b24 | (b25 << 8)] += count;
            }
            std::cout << "bytes 24..25 as u16 vs DSOUNDS:\n";
            for (const auto& [value, count] : tails) {
                if (value == 0) {
                    continue;
                }
                const auto* row = sounds.find(static_cast<std::uint32_t>(value));
                std::cout << "  " << value << " x" << count << " -> "
                          << (row != nullptr ? row->name : "(no sound)") << "\n";
            }
        }
    }

    std::cout << "bytes 16..25 by pattern:\n";
    for (const auto& [pattern, count] : middle_patterns) {
        std::cout << "  " << pattern << " x" << count << "\n";
    }

    // Are bytes 24..25 a sound id? Test the u16 against the global table.
    world::SoundTable sound_table;
    std::span<const std::byte> sound_raw;
    if (icons.payload("DSOUNDS.BIN", sound_raw) == lod::LodArchive::PayloadError::None &&
        world::SoundTable::parse(sound_raw, sound_table) == world::SoundTableError::None) {
        std::map<std::string, std::size_t> named_sounds;
        std::size_t with_value = 0;
        std::size_t resolve = 0;
        for (const auto& [pattern, count] : middle_patterns) {
            unsigned low = 0, high = 0;
            std::sscanf(pattern.c_str() + 24, "%x", &low);
            std::sscanf(pattern.c_str() + 27, "%x", &high);
            const std::uint32_t value = low | (high << 8);
            if (value == 0) {
                continue;
            }
            with_value += count;
            if (const auto* entry = sound_table.find(value); entry != nullptr) {
                resolve += count;
                named_sounds[entry->name] += count;
            }
        }
        std::cout << "bytes 24..25 as a sound id: " << resolve << "/" << with_value
                  << " nonzero values resolve\n";
        for (const auto& [sound, count] : named_sounds) {
            std::cout << "  " << sound << " x" << count << "\n";
        }
    }
    for (const auto& [pair, count] : destinations) {
        std::cout << "  " << pair << " x" << count << "\n";
    }
    std::cout << "coordinates: x " << min_coord[0] << ".." << max_coord[0] << ", y " << min_coord[1]
              << ".." << max_coord[1] << ", z " << min_coord[2] << ".." << max_coord[2]
              << ", facing 0.." << max_facing << "\n";
    for (const auto& [text, count] : unknown_names) {
        std::cout << "  not in MapStats: " << text << " x" << count << "\n";
    }
    return 0;
}

// Research mode: the shape of the would-be variable opcodes. For each, the
// argument sizes, the first byte's vocabulary (a candidate type selector),
// the little-endian u32 after it (a candidate value), and — for opcode 14 —
// whether the trailing byte stays inside its own event's sequence numbers,
// which is what a jump target would do.
int do_variables(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    struct TypeUse {
        std::size_t uses = 0;
        std::uint32_t min_value = 0xFFFFFFFF;
        std::uint32_t max_value = 0;
    };
    struct OpcodeUse {
        std::map<std::size_t, std::size_t> sizes;
        std::map<int, TypeUse> by_type;  // first byte -> value stats
        std::size_t jump_in_range = 0;   // trailing byte <= event's max sequence
        std::size_t jump_total = 0;
        std::size_t max_trailing = 0;
    };
    const std::vector<int> candidates{1, 14, 15, 16, 17, 18, 19, 32, 36, 37};
    std::map<int, OpcodeUse> opcodes;

    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (!is_script(name)) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        // The largest sequence number of each event, for the jump test.
        std::map<std::uint16_t, std::size_t> max_sequence;
        for (const auto& step : script.steps()) {
            auto& top = max_sequence[step.event_id];
            top = std::max(top, static_cast<std::size_t>(step.sequence));
        }
        for (const auto& step : script.steps()) {
            if (std::find(candidates.begin(), candidates.end(), static_cast<int>(step.opcode)) ==
                candidates.end()) {
                continue;
            }
            OpcodeUse& use = opcodes[step.opcode];
            const auto& a = step.arguments;
            ++use.sizes[a.size()];
            if (a.size() >= 5) {
                std::uint32_t value = 0;
                for (int i = 4; i >= 1; --i) {
                    value = (value << 8) | a[static_cast<std::size_t>(i)];
                }
                TypeUse& t = use.by_type[a[0]];
                ++t.uses;
                t.min_value = std::min(t.min_value, value);
                t.max_value = std::max(t.max_value, value);
            }
            if (a.size() == 6) {
                ++use.jump_total;
                use.max_trailing = std::max(use.max_trailing, static_cast<std::size_t>(a[5]));
                if (a[5] <= max_sequence[step.event_id]) {
                    ++use.jump_in_range;
                }
            }
            // The one- and two-byte opcodes: is their byte a step number too?
            if (a.size() == 1 || a.size() == 2) {
                ++use.jump_total;
                use.max_trailing = std::max(use.max_trailing, static_cast<std::size_t>(a[0]));
                if (a[0] <= max_sequence[step.event_id]) {
                    ++use.jump_in_range;
                }
                if (a.size() == 2) {
                    TypeUse& t = use.by_type[a[1]];
                    ++t.uses;
                    t.min_value = std::min(t.min_value, static_cast<std::uint32_t>(a[0]));
                    t.max_value = std::max(t.max_value, static_cast<std::uint32_t>(a[0]));
                }
            }
        }
    }

    for (const auto& [opcode, use] : opcodes) {
        std::cout << "opcode " << opcode << "\n  sizes:";
        for (const auto& [size, count] : use.sizes) {
            std::cout << "  " << size << "b x" << count;
        }
        std::cout << "\n";
        if (use.jump_total > 0) {
            std::cout << "  trailing byte within its event's sequences: " << use.jump_in_range
                      << "/" << use.jump_total << ", max " << use.max_trailing << "\n";
        }
        for (const auto& [type, t] : use.by_type) {
            std::cout << "  type " << type << "\tx" << t.uses << "\tvalue " << t.min_value << ".."
                      << t.max_value << "\n";
        }
    }
    return 0;
}

// Research mode: OUT.EVT, whose records carry no sequence byte — [size]
// [id u16][opcode][args] — unlike every map script's. Print each record, and
// decode the travel steps as spawn points.
int do_out(const starhaven::lod::LodArchive& icons, const std::string& entry_name) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    if (icons.payload(entry_name, raw) != lod::LodArchive::PayloadError::None || raw.size() < 48) {
        std::cerr << "error: no OUT.EVT\n";
        return 1;
    }
    std::uint32_t unpacked = 0;
    std::memcpy(&unpacked, raw.data() + 0x28, sizeof(unpacked));
    std::vector<std::uint8_t> payload;
    const auto body = raw.subspan(48);
    if (unpacked == 0) {
        payload.assign(reinterpret_cast<const std::uint8_t*>(body.data()),
                       reinterpret_cast<const std::uint8_t*>(body.data()) + body.size());
    } else if (!starhaven::image::detail::inflate_all(body, payload) ||
               payload.size() != unpacked) {
        std::cerr << "error: OUT.EVT container did not inflate\n";
        return 1;
    }

    std::size_t at = 0;
    while (at < payload.size()) {
        const std::size_t size = payload[at];
        if (size < 3 || at + 1 + size > payload.size()) {
            std::cout << "  misframed at byte " << at << "\n";
            break;
        }
        const std::uint16_t id =
            static_cast<std::uint16_t>(payload[at + 1] | (payload[at + 2] << 8));
        const std::uint8_t opcode = payload[at + 3];
        const std::size_t arg_count = size - 3;
        std::cout << "event " << id << "\topcode " << static_cast<int>(opcode) << " (" << arg_count
                  << " bytes)";
        if (opcode == world::kOpcodeTravel && arg_count >= 27) {
            world::ScriptStep step;
            step.opcode = world::kOpcodeTravel;
            step.arguments.assign(payload.begin() + static_cast<std::ptrdiff_t>(at + 4),
                                  payload.begin() + static_cast<std::ptrdiff_t>(at + 1 + size));
            if (const auto travel = world::parse_travel(step)) {
                std::cout << "\t-> " << (travel->destination.empty() ? "0" : travel->destination)
                          << " at " << travel->x << "," << travel->y << "," << travel->z
                          << " facing " << travel->facing;
            }
        } else {
            for (std::size_t i = 0; i < arg_count && i < 6; ++i) {
                std::cout << " " << static_cast<int>(payload[at + 4 + i]);
            }
        }
        std::cout << "\n";
        at += 1 + size;
    }
    return 0;
}

// Research mode: opcode 11, whose arguments read as a u32 and a
// NUL-terminated name. The names are texture names, testable against
// BITMAPS.LOD; the u32 reads as the face to re-texture.
int do_sounds(const starhaven::lod::LodArchive& icons, const starhaven::lod::LodArchive& bitmaps) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t uses = 0;
    std::size_t named = 0;
    std::size_t name_is_bitmap = 0;
    std::uint32_t max_id = 0;
    std::map<std::string, std::size_t> unresolved;
    std::map<std::string, std::size_t> resolved;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 11) {
                continue;
            }
            ++uses;
            const auto& a = step.arguments;
            if (a.size() < 5) {
                continue;
            }
            std::uint32_t id = 0;
            for (int i = 3; i >= 0; --i) {
                id = (id << 8) | a[static_cast<std::size_t>(i)];
            }
            max_id = std::max(max_id, id);
            std::string name;
            for (std::size_t i = 4; i < a.size() && a[i] != 0; ++i) {
                name += static_cast<char>(a[i]);
            }
            if (name.empty()) {
                continue;
            }
            ++named;
            if (bitmaps.find(name).has_value()) {
                ++name_is_bitmap;
                ++resolved[name];
            } else {
                ++unresolved[entry.name + " face " + std::to_string(id) + " \"" + name + "\""];
            }
        }
    }
    std::cout << uses << " uses of opcode 11; " << named << " carry a name, " << name_is_bitmap
              << " of those are BITMAPS.LOD entries; face ids run to " << max_id << "\n";
    std::cout << resolved.size() << " distinct textures:\n";
    for (const auto& [name, count] : resolved) {
        std::cout << "  " << name << " x" << count << "\n";
    }
    for (const auto& [example, count] : unresolved) {
        std::cout << "  not a bitmap: " << example << " x" << count << "\n";
    }
    return 0;
}

// Research mode: the events with no opcode-4 header. Where do they live,
// what do they open with, and do the face event ids that resolve nowhere
// point at the shared scripts?
int do_unheaded(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::map<std::string, std::pair<std::size_t, std::size_t>> per_script;  // headed, unheaded
    std::map<int, std::size_t> unheaded_opens;
    std::set<std::uint16_t> global_ids;
    std::map<std::string, world::MapScript> scripts;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        std::uint16_t last = 0xFFFF;
        for (const auto& step : script.steps()) {
            if (step.event_id == last) {
                continue;
            }
            last = step.event_id;
            const bool headed = step.opcode == world::kOpcodeHeader;
            auto& [h, u] = per_script[stem];
            (headed ? h : u) += 1;
            if (!headed) {
                ++unheaded_opens[step.opcode];
            }
            if (stem == "GLOBAL") {
                global_ids.insert(step.event_id);
            }
        }
        scripts.emplace(stem, std::move(script));
    }

    std::size_t headed_total = 0;
    std::size_t unheaded_total = 0;
    std::cout << "unheaded events by script (only scripts that have them):\n";
    for (const auto& [stem, counts] : per_script) {
        headed_total += counts.first;
        unheaded_total += counts.second;
        if (counts.second > 0) {
            std::cout << "  " << stem << ": " << counts.second << " unheaded, " << counts.first
                      << " headed\n";
        }
    }
    std::cout << headed_total << " headed, " << unheaded_total << " unheaded\n";
    std::cout << "unheaded events open with:";
    for (const auto& [opcode, count] : unheaded_opens) {
        std::cout << "  op" << opcode << " x" << count;
    }
    std::cout << "\n";

    // The 33 face event ids that resolve in no map script: are they GLOBAL's?
    lod::GameLodArchive games;
    if (lod::GameLodArchive::open(*starhaven::platform::install_from_env() / "data" / "Games.lod",
                                  games) != lod::GameLodError::None) {
        return 0;
    }
    std::size_t faces_unresolved = 0;
    std::size_t in_global = 0;
    for (const auto& [stem, script] : scripts) {
        std::span<const std::byte> level;
        world::BlvMap blv;
        if (games.payload(stem + ".blv", level) != lod::GameLodArchive::PayloadError::None ||
            world::parse_blv(level, blv) != world::BlvError::None) {
            continue;
        }
        for (const auto& extra : blv.face_extras) {
            if (extra.event_id == 0 || script.defines(extra.event_id)) {
                continue;
            }
            ++faces_unresolved;
            in_global += global_ids.contains(extra.event_id) ? 1 : 0;
        }
    }
    std::cout << faces_unresolved << " face event ids resolve in no map script; " << in_global
              << " of those are GLOBAL.EVT events\n";

    // And the join the letter quest exposed: an NPC row's event ids against
    // the global script's own event ids.
    starhaven::data::NpcTable npcs;
    if (starhaven::data::load_npcs(*starhaven::platform::install_from_env() / "data", npcs) ==
        starhaven::data::GameDataError::None) {
        std::size_t npc_events = 0;
        std::size_t npc_in_global = 0;
        for (const auto& npc : npcs.entries()) {
            for (const int id : npc.events) {
                if (id <= 0) {
                    continue;
                }
                ++npc_events;
                npc_in_global += global_ids.contains(static_cast<std::uint16_t>(id)) ? 1 : 0;
            }
        }
        std::cout << npc_events << " NPC event ids; " << npc_in_global
                  << " are GLOBAL.EVT events\n";
    }
    return 0;
}

// Research mode: which unnamed opcode plays a sound? For every opcode and
// every u32 offset inside its arguments, the resolve rate against the global
// sound table — with the names, since a rate alone pattern-matches on the
// dense id regions.
int do_soundsweep(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::SoundTable sounds;
    std::span<const std::byte> raw;
    if (icons.payload("DSOUNDS.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SoundTable::parse(raw, sounds) != world::SoundTableError::None) {
        std::cerr << "error: could not read DSOUNDS.BIN\n";
        return 1;
    }

    struct Candidate {
        std::size_t uses = 0;
        std::size_t resolves = 0;
        std::set<std::uint32_t> values;
        std::map<std::string, std::size_t> names;
    };
    const std::set<int> named{1, 2, 4, 5, 6, 7, 11, 14, 15, 16, 17, 18, 29, 30, 35, 36};
    std::map<std::pair<int, int>, Candidate> candidates;  // (opcode, offset)
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (named.contains(step.opcode) || step.arguments.size() < 4) {
                continue;
            }
            for (std::size_t at = 0; at + 4 <= step.arguments.size(); ++at) {
                std::uint32_t value = 0;
                for (int i = 3; i >= 0; --i) {
                    value = (value << 8) | step.arguments[at + static_cast<std::size_t>(i)];
                }
                if (value == 0) {
                    continue;
                }
                Candidate& c = candidates[{step.opcode, static_cast<int>(at)}];
                ++c.uses;
                if (const auto* found = sounds.find(value); found != nullptr) {
                    ++c.resolves;
                    c.values.insert(value);
                    ++c.names[found->name];
                }
            }
        }
    }

    for (const auto& [key, c] : candidates) {
        if (c.uses < 10 || c.resolves * 2 < c.uses || c.values.size() < 3) {
            continue;  // report only near-total resolve rates with variety
        }
        std::cout << "opcode " << key.first << " +[" << key.second << "]: " << c.resolves << "/"
                  << c.uses << " resolve, " << c.values.size() << " distinct;";
        std::size_t shown = 0;
        for (const auto& [name, count] : c.names) {
            if (++shown > 6) {
                break;
            }
            std::cout << " " << name << " x" << count;
        }
        std::cout << "\n";
    }
    return 0;
}

// Research mode: opcode 4, correlated against what its event's body does.
// For every headed event, compare the header's argument with the argument of
// each kind of working step the body contains.
int do_headers(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    struct Match {
        std::size_t events = 0;
        std::size_t agree = 0;
    };
    std::map<int, Match> by_body;  // body opcode -> header arg equality
    std::map<int, std::size_t> header_values;
    // The other candidate: the header as an index into the map's own .STR —
    // the thing's mouseover name. Measured separately for events that open an
    // establishment (whose header is the 2DEvents row) and the rest.
    std::size_t plain_events = 0, plain_named = 0, enter_events = 0, enter_named = 0;
    std::map<std::string, std::size_t> plain_names;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        world::MapStrings strings;
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        std::uint16_t last = 0xFFFF;
        for (std::size_t i = 0; i < script.steps().size(); ++i) {
            const auto& head = script.steps()[i];
            if (head.event_id == last) {
                continue;
            }
            last = head.event_id;
            if (head.opcode != world::kOpcodeHeader || head.arguments.empty()) {
                continue;
            }
            const int header = head.arguments.front();
            ++header_values[header];
            bool has_enter = false;
            for (std::size_t k = i + 1;
                 k < script.steps().size() && script.steps()[k].event_id == head.event_id; ++k) {
                has_enter = has_enter || script.steps()[k].opcode == world::kOpcodeEnter;
            }
            const std::string_view named =
                static_cast<std::size_t>(header) < strings.size() ? strings.at(header) : "";
            if (has_enter) {
                ++enter_events;
                enter_named += named.empty() ? 0 : 1;
            } else {
                ++plain_events;
                if (!named.empty()) {
                    ++plain_named;
                    ++plain_names[std::string(named)];
                }
            }
            // The body's working steps, each kind counted once per event.
            std::set<int> seen;
            for (std::size_t k = i + 1;
                 k < script.steps().size() && script.steps()[k].event_id == head.event_id; ++k) {
                const auto& step = script.steps()[k];
                if (step.arguments.empty() || !seen.insert(step.opcode).second) {
                    continue;
                }
                Match& match = by_body[step.opcode];
                ++match.events;
                match.agree += step.arguments.front() == header ? 1 : 0;
            }
        }
    }
    std::cout << "header arg vs first arg of each body opcode:\n";
    for (const auto& [opcode, match] : by_body) {
        if (match.events < 20) {
            continue;
        }
        std::cout << "  op" << opcode << ": " << match.agree << "/" << match.events << "\n";
    }
    std::cout << "header as .STR index: names " << plain_named << "/" << plain_events
              << " non-establishment events, " << enter_named << "/" << enter_events
              << " establishment events; the names:\n";
    {
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [name, count] : plain_names) {
            ranked.emplace_back(count, name);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::size_t listed = 0;
        for (const auto& [count, name] : ranked) {
            if (++listed > 20) {
                std::cout << "  ...\n";
                break;
            }
            std::cout << "  \"" << name << "\" x" << count << "\n";
        }
    }
    std::cout << "header values:";
    std::size_t shown = 0;
    for (const auto& [value, count] : header_values) {
        if (++shown > 24) {
            std::cout << " ...";
            break;
        }
        std::cout << " " << value << " x" << count;
    }
    std::cout << "\n";
    return 0;
}

// Research mode: opcodes 39 and 40, read as NPC mutations and tested against
// the NPC table's own id and topic-slot spaces.
int do_npc_mutations(const starhaven::lod::LodArchive& icons,
                     const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::NpcTable npcs;
    if (data::load_npcs(data_dir, npcs) != data::GameDataError::None) {
        std::cerr << "error: could not read NPCdata.txt\n";
        return 1;
    }
    data::NpcDialogueTable dialogue;
    (void)data::load_npc_dialogue(data_dir, dialogue);
    const std::size_t npc_count = npcs.entries().size();

    std::span<const std::byte> raw;
    const auto u32_at = [](const std::vector<std::uint8_t>& a, std::size_t at) {
        std::uint32_t value = 0;
        for (int i = 3; i >= 0; --i) {
            value = (value << 8) | a[at + static_cast<std::size_t>(i)];
        }
        return value;
    };

    std::size_t op39_uses = 0, op39_npc_ok = 0, op39_topic_ok = 0;
    std::map<int, std::size_t> op39_slots;
    std::size_t op40_uses = 0, op40_npc_ok = 0, op40_place_ok = 0, op40_zero = 0;
    std::uint32_t op40_max_place = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode == 39 && step.arguments.size() >= 9) {
                ++op39_uses;
                const std::uint32_t npc = u32_at(step.arguments, 0);
                const int slot = step.arguments[4];
                const std::uint32_t topic = u32_at(step.arguments, 5);
                op39_npc_ok += npc >= 1 && npc <= npc_count ? 1 : 0;
                ++op39_slots[slot];
                op39_topic_ok +=
                    dialogue.at(static_cast<int>(topic)) != nullptr || topic == 0 ? 1 : 0;
                if (dialogue.at(static_cast<int>(topic)) == nullptr && topic != 0)
                    std::cout << "  unresolved topic " << topic << "\n";
            } else if (step.opcode == 40 && step.arguments.size() >= 8) {
                ++op40_uses;
                const std::uint32_t npc = u32_at(step.arguments, 0);
                const std::uint32_t place = u32_at(step.arguments, 4);
                op40_npc_ok += npc >= 1 && npc <= npc_count ? 1 : 0;
                op40_zero += place == 0 ? 1 : 0;
                op40_place_ok += place >= 1 && place <= 557 ? 1 : 0;
                op40_max_place = std::max(op40_max_place, place);
            }
        }
    }
    std::cout << "op39 [npc u32][slot u8][topic u32]: " << op39_uses << " uses, npc in 1.."
              << npc_count << " on " << op39_npc_ok << ", topic resolves on " << op39_topic_ok
              << "; slots:";
    for (const auto& [slot, count] : op39_slots) {
        std::cout << " " << slot << " x" << count;
    }
    std::cout << "\n";
    std::cout << "op40 [npc u32][place u32]: " << op40_uses << " uses, npc in range on "
              << op40_npc_ok << ", place zero on " << op40_zero << ", place in 1..557 on "
              << op40_place_ok << ", max place " << op40_max_place << "\n";
    return 0;
}

// Research mode: the remaining common opcodes, each use printed compactly
// with its script and event, decoded by its candidate shape.
int do_catalog(const starhaven::lod::LodArchive& icons, int wanted) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t shown = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (static_cast<int>(step.opcode) != wanted || shown >= 2200) {
                continue;
            }
            ++shown;
            std::cout << entry.name.substr(0, entry.name.size() - 4) << " " << step.event_id << ":";
            const auto& a = step.arguments;
            if (wanted == 19 && a.size() >= 15) {
                // Candidate shape: a kind, then three i32 coordinates.
                const auto i32_at = [&a](std::size_t at) {
                    std::uint32_t v = 0;
                    for (int i = 3; i >= 0; --i) {
                        v = (v << 8) | a[at + static_cast<std::size_t>(i)];
                    }
                    return static_cast<std::int32_t>(v);
                };
                std::cout << " (" << static_cast<int>(a[0]) << "," << static_cast<int>(a[1]) << ","
                          << static_cast<int>(a[2]) << ") at " << i32_at(3) << "," << i32_at(7)
                          << "," << i32_at(11);
            } else {
                for (const std::uint8_t b : a) {
                    std::cout << " " << static_cast<int>(b);
                }
            }
            std::cout << "\n";
        }
    }
    std::cout << shown << " shown\n";
    return 0;
}

// Research mode: opcode 21's u16 against the object table's own ids — a
// projectile has to be some object.
int do_projectiles(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::ObjectTable objects;
    std::span<const std::byte> raw;
    if (icons.payload("DOBJLIST.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::ObjectTable::parse(raw, objects) != world::ObjectTableError::None) {
        std::cerr << "error: could not read DOBJLIST.BIN\n";
        return 1;
    }
    std::set<std::uint16_t> ids;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (const auto* d = objects.at(i); d != nullptr) {
            ids.insert(d->object_id);
        }
    }

    std::size_t uses = 0, resolve = 0;
    std::map<std::string, std::size_t> names;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if (step.opcode != 21 || step.arguments.size() < 27) {
                continue;
            }
            ++uses;
            const auto id =
                static_cast<std::uint16_t>(step.arguments[0] | (step.arguments[1] << 8));
            if (!ids.contains(id)) {
                continue;
            }
            ++resolve;
            for (std::size_t i = 0; i < objects.size(); ++i) {
                if (const auto* d = objects.at(i); d != nullptr && d->object_id == id) {
                    ++names[d->name + " (" + std::to_string(id) + ")"];
                    break;
                }
            }
        }
    }
    std::cout << uses << " uses of opcode 21; the u16 is an object id on " << resolve << "\n";
    for (const auto& [name, count] : names) {
        std::cout << "  " << name << " x" << count << "\n";
    }
    return 0;
}

// Research mode: opcode 21's u16 against the sprite frame table. Two candidate
// readings — the Nth group in file order, or a raw frame index — printed side
// by side with the two points, so the names and the maps can judge.
int do_launches(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    world::SpriteFrameTable table;
    std::span<const std::byte> raw;
    if (icons.payload("DSFT.BIN", raw) != lod::LodArchive::PayloadError::None ||
        world::SpriteFrameTable::parse(raw, table) != world::SpriteFrameError::None) {
        std::cerr << "error: could not read DSFT.BIN\n";
        return 1;
    }
    // The Nth group's name, in the order the file stores its frames.
    std::vector<std::string> group_names;
    for (const auto& frame : table.frames()) {
        if (!frame.group_name.empty()) {
            group_names.push_back(frame.group_name);
        }
    }

    std::size_t uses = 0, group_hits = 0, frame_start_hits = 0;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            const auto& a = step.arguments;
            if (step.opcode != 21 || a.size() < 27) {
                continue;
            }
            ++uses;
            const auto id = static_cast<std::uint16_t>(a[0] | (a[1] << 8));
            const auto i32_at = [&a](std::size_t at) {
                std::uint32_t v = 0;
                for (int i = 3; i >= 0; --i) {
                    v = (v << 8) | a[at + static_cast<std::size_t>(i)];
                }
                return static_cast<std::int32_t>(v);
            };
            const bool as_group = id < group_names.size();
            const bool as_frame_start = id < table.size() && table.frames()[id].starts_group();
            group_hits += as_group ? 1 : 0;
            frame_start_hits += as_frame_start ? 1 : 0;
            std::cout << entry.name.substr(0, entry.name.size() - 4) << " " << step.event_id
                      << ": group " << id << " = "
                      << (as_group ? group_names[id] : std::string("<out of range>"))
                      << (as_frame_start ? " (as frame: " + table.frames()[id].group_name + ")"
                                         : " (as frame: not a group start)")
                      << " speed? " << static_cast<int>(a[2]) << " from " << i32_at(3) << ","
                      << i32_at(7) << "," << i32_at(11) << " to " << i32_at(15) << "," << i32_at(19)
                      << "," << i32_at(23) << "\n";
        }
    }
    std::cout << uses << " uses; u16 in group range on " << group_hits
              << ", starts a group as a frame index on " << frame_start_hits << "\n";
    return 0;
}

// Research mode: the three shelf opcodes, each against its candidate reading.
// 26 as a typed-answer ask: three string indices and a success step. 25 as a
// random jump: up to six step numbers, zero-padded. 32 as an event switch:
// an event id of this same script, and on or off.
int do_asks(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    std::span<const std::byte> raw;
    std::size_t ask_uses = 0, ask_strings = 0, ask_steps = 0, ask_echo = 0;
    std::size_t jump_uses = 0, jump_values = 0, jump_steps = 0;
    std::size_t switch_uses = 0, switch_events = 0, switch_flags = 0, switch_global = 0,
                switch_zero = 0;
    world::MapScript global_script;
    if (icons.payload("GLOBAL.EVT", raw) == lod::LodArchive::PayloadError::None) {
        (void)world::MapScript::parse(raw, global_script);
    }
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        world::MapStrings strings;
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        const auto step_of = [&script](std::uint16_t event, std::uint8_t sequence) {
            for (const auto& s : script.event(event)) {
                if (s.sequence == sequence && s.opcode != world::kOpcodeHeader) {
                    return true;
                }
            }
            return false;
        };
        const auto u32_at = [](const std::vector<std::uint8_t>& a, std::size_t at) {
            std::uint32_t value = 0;
            for (int i = 3; i >= 0; --i) {
                value = (value << 8) | a[at + static_cast<std::size_t>(i)];
            }
            return value;
        };
        for (const auto& step : script.steps()) {
            const auto& a = step.arguments;
            if (step.opcode == 26 && a.size() >= 13) {
                ++ask_uses;
                const std::uint32_t prompt = u32_at(a, 0);
                const std::uint32_t first = u32_at(a, 4);
                const std::uint32_t second = u32_at(a, 8);
                const bool resolve =
                    prompt < strings.size() && first < strings.size() && second < strings.size();
                ask_strings += resolve ? 1 : 0;
                ask_steps += step_of(step.event_id, a[12]) ? 1 : 0;
                if (resolve) {
                    // The two answers as one word: equal, or apart only by
                    // case or an article.
                    std::string low1, low2;
                    for (const char c : strings.at(first)) {
                        low1 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    for (const char c : strings.at(second)) {
                        low2 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    ask_echo += low1 == low2 || low2.find(low1) != std::string::npos ||
                                        low1.find(low2) != std::string::npos
                                    ? 1
                                    : 0;
                    std::cout << stem << " " << step.event_id << ": \"" << strings.at(prompt)
                              << "\" -> \"" << strings.at(first) << "\" / \"" << strings.at(second)
                              << "\"\n";
                }
            } else if (step.opcode == 25 && a.size() >= 6) {
                ++jump_uses;
                for (std::size_t i = 0; i < 6; ++i) {
                    if (a[i] == 0) {
                        continue;
                    }
                    ++jump_values;
                    jump_steps += step_of(step.event_id, a[i]) ? 1 : 0;
                }
            } else if (step.opcode == 32 && a.size() >= 5) {
                ++switch_uses;
                const auto id = static_cast<std::uint16_t>(u32_at(a, 0));
                const bool defined = script.defines(id);
                switch_events += defined ? 1 : 0;
                switch_zero += id == 0 ? 1 : 0;
                if (!defined && id != 0) {
                    switch_global += global_script.defines(id) ? 1 : 0;
                    if (!global_script.defines(id)) {
                        std::cout << "op32 miss: " << stem << " " << step.event_id << " -> " << id
                                  << "\n";
                    }
                }
                switch_flags += a[4] <= 1 ? 1 : 0;
            }
        }
    }
    std::cout << "op26 [prompt u32][answer u32][answer u32][step u8]: " << ask_uses
              << " full uses; all three strings resolve on " << ask_strings
              << ", the step exists on " << ask_steps << ", the answers echo on " << ask_echo
              << "\n";
    std::cout << "op25 [step u8 x6, zero-padded]: " << jump_uses << " uses, " << jump_values
              << " nonzero entries, " << jump_steps << " are steps of their own event\n";
    std::cout << "op32 [event u32][on/off u8]: " << switch_uses
              << " uses; the id is an event of this script on " << switch_events
              << ", of GLOBAL.EVT on another " << switch_global << ", zero on " << switch_zero
              << "; the byte is 0/1 on " << switch_flags << "\n";
    return 0;
}

// Research mode: the give/take/set types still unnamed, joined against the
// numbers their own events speak. A quest's reward step and its reward prose
// sit in the same event; when the prose says "500 experience" and a give of
// an unnamed type carries 500, the type has told its name.
int do_currencies(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::NpcDialogueTable dialogue;
    (void)data::load_npc_dialogue(data_dir, dialogue);

    // type -> word spoken with a matching number -> how often.
    std::map<int, std::map<std::string, std::size_t>> matches;
    std::map<int, std::size_t> uses;

    std::span<const std::byte> raw;
    for (const auto& entry : icons.entries()) {
        if (!is_script(entry.name)) {
            continue;
        }
        world::MapScript script;
        if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        const std::string stem = entry.name.substr(0, entry.name.size() - 4);
        world::MapStrings strings;
        if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapStrings::parse(raw, strings);
        }
        const bool global = stem == "GLOBAL";

        std::uint16_t last = 0xFFFF;
        for (std::size_t i = 0; i < script.steps().size(); ++i) {
            if (script.steps()[i].event_id == last) {
                continue;
            }
            last = script.steps()[i].event_id;
            const auto steps = script.event(last);

            // Everything this event says, in one string.
            std::string prose;
            for (const auto& step : steps) {
                if ((step.opcode != world::kOpcodeMessage &&
                     step.opcode != world::kOpcodeLongMessage) ||
                    step.arguments.empty()) {
                    continue;
                }
                const int index = step.arguments.front();
                if (global) {
                    if (const auto* row = dialogue.at(index); row != nullptr) {
                        prose += row->text + " ";
                    }
                } else if (static_cast<std::size_t>(index) < strings.size()) {
                    prose += std::string(strings.at(static_cast<std::size_t>(index))) + " ";
                }
            }

            // The numbers it speaks, each with the word that follows.
            std::vector<std::pair<long, std::string>> spoken;
            for (std::size_t at = 0; at < prose.size(); ++at) {
                if (std::isdigit(static_cast<unsigned char>(prose[at])) == 0) {
                    continue;
                }
                long value = 0;
                std::size_t p = at;
                while (p < prose.size() &&
                       (std::isdigit(static_cast<unsigned char>(prose[p])) != 0 ||
                        (prose[p] == ',' && p + 1 < prose.size() &&
                         std::isdigit(static_cast<unsigned char>(prose[p + 1])) != 0))) {
                    if (prose[p] != ',') {
                        value = value * 10 + (prose[p] - '0');
                    }
                    ++p;
                }
                while (p < prose.size() && prose[p] == ' ') {
                    ++p;
                }
                std::string word;
                while (p < prose.size() &&
                       std::isalpha(static_cast<unsigned char>(prose[p])) != 0) {
                    word += static_cast<char>(std::tolower(static_cast<unsigned char>(prose[p])));
                    ++p;
                }
                spoken.emplace_back(value, word);
                at = p;
            }

            // The unnamed types this event gives, takes or sets.
            for (const auto& step : steps) {
                if ((step.opcode != world::kOpcodeGive && step.opcode != world::kOpcodeTake &&
                     step.opcode != world::kOpcodeSet) ||
                    step.arguments.size() < 5) {
                    continue;
                }
                const int type = step.arguments[0];
                if (type == world::kVarQuestBit || type == world::kVarItem ||
                    type == world::kVarGold) {
                    continue;
                }
                std::uint32_t value = 0;
                for (std::size_t b = 4; b >= 1; --b) {
                    value = (value << 8) | step.arguments[b];
                }
                ++uses[type];
                for (const auto& [number, word] : spoken) {
                    if (number == static_cast<long>(value) && !word.empty()) {
                        ++matches[type][word];
                    }
                }
            }
        }
    }

    // Type 12 against `Awards.txt`: the range fits the filled rows, and the
    // one value a known quest sets — Goblinwatch's 53 — names exactly
    // "Solved the Goblinwatch Combination". Count the whole join.
    {
        data::JournalTable awards;
        if (data::load_awards(data_dir, awards) == data::GameDataError::None) {
            std::set<int> filled;
            for (const auto& row : awards.entries()) {
                if (row.has_text()) {
                    filled.insert(row.bit);
                }
            }
            std::size_t uses = 0, hits = 0;
            std::map<int, std::size_t> misses;
            std::size_t fame_uses = 0;
            long fame_min = 0, fame_max = 0;
            for (const auto& entry : icons.entries()) {
                if (!is_script(entry.name)) {
                    continue;
                }
                world::MapScript script;
                if (icons.payload(entry.name, raw) != lod::LodArchive::PayloadError::None ||
                    world::MapScript::parse(raw, script) != world::MapScriptError::None) {
                    continue;
                }
                for (const auto& step : script.steps()) {
                    const bool typed =
                        step.opcode == world::kOpcodeCheck || step.opcode == world::kOpcodeGive ||
                        step.opcode == world::kOpcodeTake || step.opcode == world::kOpcodeSet;
                    if (!typed || step.arguments.size() < 5) {
                        continue;
                    }
                    std::uint32_t value = 0;
                    for (std::size_t b = 4; b >= 1; --b) {
                        value = (value << 8) | step.arguments[b];
                    }
                    if (step.arguments[0] == 12) {
                        ++uses;
                        if (filled.contains(static_cast<int>(value))) {
                            ++hits;
                        } else {
                            ++misses[static_cast<int>(value)];
                        }
                    } else if (step.arguments[0] == 22) {
                        ++fame_uses;
                        fame_min = fame_uses == 1 ? static_cast<long>(value)
                                                  : std::min(fame_min, static_cast<long>(value));
                        fame_max = std::max(fame_max, static_cast<long>(value));
                    }
                }
            }
            std::cout << "type 12 vs Awards.txt filled rows: " << hits << "/" << uses;
            for (const auto& [value, count] : misses) {
                std::cout << " (miss " << value << " x" << count << ")";
            }
            std::cout << "\ntype 22: " << fame_uses << " uses, values " << fame_min << ".."
                      << fame_max << "\n";
        }
    }

    for (const auto& [type, words] : matches) {
        std::cout << "type " << type << " (" << uses[type] << " uses):";
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [word, count] : words) {
            ranked.emplace_back(count, word);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::size_t shown = 0;
        for (const auto& [count, word] : ranked) {
            if (++shown > 6) {
                break;
            }
            std::cout << " " << word << " x" << count;
        }
        std::cout << "\n";
    }
    return 0;
}

// Audit mode: every award row against every script's gives. Award 35
// taught the lesson — some honors are granted outside the scripts — so
// the orphans become a measured list instead of surprises.
int do_ledger(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir,
              bool walk) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;

    data::JournalTable awards;
    if (data::load_awards(data_dir, awards) != data::GameDataError::None) {
        std::cerr << "ledger: no Awards.txt\n";
        return 1;
    }
    // award id -> "SCRIPT event N" of its first grantor, and the parsed
    // script + event for the walking pass.
    std::map<int, std::string> grantor;
    std::map<int, std::pair<world::MapScript, std::uint16_t>> grantor_event;
    for (const auto& entry : icons.entries()) {
        const std::string& name = entry.name;
        if (name.size() < 4 ||
            (name.substr(name.size() - 4) != ".EVT" && name.substr(name.size() - 4) != ".evt")) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapScript script;
        if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, script) != world::MapScriptError::None) {
            continue;
        }
        for (const auto& step : script.steps()) {
            if ((step.opcode != world::kOpcodeGive && step.opcode != world::kOpcodeSet) ||
                step.arguments.size() < 3 || step.arguments[0] != 12) {
                continue;
            }
            const int award = step.arguments[1] | (step.arguments[2] << 8);
            if (!grantor.contains(award)) {
                grantor[award] = name + " event " + std::to_string(step.event_id);
                grantor_event[award] = {script, step.event_id};
            }
        }
    }
    // The walking pass: run each grantor with a state built to satisfy
    // its own checks — every bit, item, award and variable a check names
    // is granted up front — and once bare, so a grant on either branch
    // counts. An event that walks without ever granting is a named gap.
    const auto walks_and_grants = [](const world::MapScript& script, std::uint16_t event,
                                     int award) {
        starhaven::game::WalkState satisfied;
        satisfied.gold = 1000000;
        for (const auto& step : script.event(event)) {
            if (step.opcode != world::kOpcodeCheck || step.arguments.size() < 6) {
                continue;
            }
            const int type = step.arguments[0];
            const int value = step.arguments[1] | (step.arguments[2] << 8);
            if (type == world::kVarQuestBit) {
                satisfied.bits.insert(value);
            } else if (type == world::kVarItem) {
                satisfied.items.push_back(value);
            } else if (type == world::kVarAward) {
                satisfied.awards.insert(value);
            } else if (type != world::kVarGold) {
                satisfied.variables[type] = value;
            }
        }
        // Doors tried: everything satisfied, nothing satisfied, and each
        // single fact left out in turn — the honorary promotions grant
        // only when the class check fails while the deed holds, and some
        // once-only chains need their own done-bit left unset.
        auto try_state = [&](starhaven::game::WalkState state) {
            const auto outcome = starhaven::game::walk_event(script, event, state);
            return outcome.ran && state.awards.contains(award);
        };
        if (try_state(satisfied)) {
            return true;
        }
        if (try_state(starhaven::game::WalkState{})) {
            return true;
        }
        for (const int bit : std::set<int>(satisfied.bits)) {
            starhaven::game::WalkState leave = satisfied;
            leave.bits.erase(bit);
            if (try_state(std::move(leave))) {
                return true;
            }
        }
        for (const auto& [type, value] : std::map<int, int>(satisfied.variables)) {
            starhaven::game::WalkState leave = satisfied;
            leave.variables.erase(type);
            if (try_state(std::move(leave))) {
                return true;
            }
            // The class checks read as equality in the original; a value
            // past the checked one fails them where absence cannot.
            leave = satisfied;
            leave.variables[type] = value + 1000;
            if (try_state(std::move(leave))) {
                return true;
            }
        }
        return false;
    };

    std::size_t granted = 0, orphans = 0, walked = 0, stuck = 0;
    for (const auto& row : awards.entries()) {
        if (!row.has_text()) {
            continue;
        }
        const auto it = grantor.find(row.bit);
        if (it != grantor.end()) {
            ++granted;
            std::string note;
            if (walk) {
                const auto& [script, event] = grantor_event[row.bit];
                if (walks_and_grants(script, event, row.bit)) {
                    ++walked;
                } else {
                    ++stuck;
                    note = "  WALKS BUT NEVER GRANTS";
                }
            }
            std::cout << "  " << row.bit << "  " << it->second << note << "\n";
        } else {
            ++orphans;
            std::cout << "  " << row.bit << "  NO SCRIPT GRANTS THIS  ("
                      << starhaven::data::cp1252_to_utf8(row.text) << ")\n";
        }
    }
    std::cout << granted << " awards granted by scripts, " << orphans << " orphans";
    if (walk) {
        std::cout << "; " << walked << " grantors walk and grant, " << stuck << " stick";
    }
    std::cout << "\n";
    return stuck > 0 ? 1 : 0;
}

// Verification mode: the New Sorpigal opening arc, walked end to end
// through the same code the game runs. Exits nonzero on the first beat
// that fails, so it can stand as a regression against a real install.
int do_arc(const starhaven::lod::LodArchive& icons, const std::filesystem::path& data_dir) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace data = starhaven::data;
    namespace game = starhaven::game;

    std::span<const std::byte> raw;
    world::MapScript global;
    if (icons.payload("GLOBAL.EVT", raw) != lod::LodArchive::PayloadError::None ||
        world::MapScript::parse(raw, global) != world::MapScriptError::None) {
        std::cerr << "arc: no GLOBAL.EVT\n";
        return 1;
    }
    std::size_t passed = 0;
    const auto beat = [&passed](bool ok, const char* what) {
        std::cout << (ok ? "  ok  " : "  FAIL") << "  " << what << "\n";
        if (ok) {
            ++passed;
        }
        return ok;
    };

    // 1. The letter refused without the letter.
    {
        game::WalkState state;
        state.bits.insert(81);
        const auto outcome = game::walk_event(global, 1, state);
        if (!beat(outcome.ran && state.gold == 0 && outcome.taken.empty() &&
                      state.resolved_quests.empty(),
                  "Andover only talks about the letter until it is held")) {
            return 1;
        }
    }
    // 2. The delivery pays, rotates the topic and moves the bits.
    {
        game::WalkState state;
        state.bits.insert(81);
        state.items.push_back(505);
        const auto outcome = game::walk_event(global, 1, state);
        // The event never takes the letter — its steps hold no item-take —
        // so the scroll stays a keepsake; only the bits and the topic move.
        const bool ok = outcome.ran && state.gold == 1000 && state.npc_topics.at({1, 0}) == 2 &&
                        !state.bits.contains(81) && state.bits.contains(82) &&
                        state.resolved_quests.contains(81) && !state.resolved_quests.contains(82) &&
                        std::find(state.items.begin(), state.items.end(), 505) != state.items.end();
        if (!beat(ok, "the letter pays 1000, rotates Andover and moves bit 81 to 82")) {
            return 1;
        }
    }
    // 3. Goblinwatch's reward: the combination scroll earns the award, the
    //    gold and the experience.
    {
        game::WalkState state;
        state.items.push_back(543);
        const auto outcome = game::walk_event(global, 4, state);
        // This reward, too, checks its item without taking it.
        const bool ok = outcome.ran && state.awards.contains(53) && state.gold == 2000 &&
                        state.experience == 2000;
        if (!beat(ok, "the combination scroll earns award 53, 2000 gold and 2000 experience")) {
            return 1;
        }
    }
    // 4. Goblinwatch's levers throw doors once, and hold after.
    {
        world::MapScript d01;
        if (icons.payload("D01.EVT", raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, d01) != world::MapScriptError::None) {
            std::cerr << "arc: no D01.EVT\n";
            return 1;
        }
        // A plain door opens every use; a lever is the event that checks
        // its own variable before throwing — the first with both steps.
        std::uint16_t lever = 0;
        for (const auto& step : d01.steps()) {
            if (step.opcode != world::kOpcodeDoor) {
                continue;
            }
            bool gated = false;
            for (const auto& other : d01.event(step.event_id)) {
                gated = gated || other.opcode == world::kOpcodeCheck;
            }
            if (gated) {
                lever = step.event_id;
                break;
            }
        }
        game::WalkState state;
        const auto first = game::walk_event(d01, lever, state);
        const auto second = game::walk_event(d01, lever, state);
        const bool ok = lever != 0 && !first.doors.empty() && second.doors.empty();
        if (!beat(ok, "a Goblinwatch lever throws its doors once and holds")) {
            return 1;
        }
    }
    // 5. The coach at New Sorpigal reaches Castle Ironfist for a fare.
    {
        data::BuildingStatsTable buildings;
        data::MapStatsTable maps;
        if (data::load_building_stats(data_dir, buildings) != data::GameDataError::None ||
            data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
            std::cerr << "arc: no tables\n";
            return 1;
        }
        bool reaches = false;
        int fare = 0;
        for (const auto* shop : buildings.on_map("E3")) {
            if (!game::is_travel(*shop)) {
                continue;
            }
            for (const auto& route : game::routes_of(*shop, maps)) {
                if (route.map_file == "OutD3.Odm") {
                    reaches = true;
                    fare = game::fare_of(*shop);
                }
            }
        }
        if (!beat(reaches && fare > 0, "a New Sorpigal coach is priced through to Ironfist")) {
            return 1;
        }
    }
    // 6. The Fire guild's shelves and roll both resolve.
    {
        data::JournalTable awards;
        if (data::load_awards(data_dir, awards) != data::GameDataError::None) {
            std::cerr << "arc: no Awards.txt\n";
            return 1;
        }
        const bool ok = game::guild_award_of(data::SpellSchool::Fire, awards) == 74;
        if (!beat(ok, "the Fire guild's membership is award 74, Joined the Fire Guild")) {
            return 1;
        }
    }
    // 7. The Seer points at each stage in the chain's own words.
    {
        game::WalkState state;
        state.bits.insert(81);
        const auto before = game::walk_event(global, 46, state);
        game::WalkState later;
        later.bits.insert(82);
        const auto after = game::walk_event(global, 46, later);
        // npctext.txt rows 436 and 428: "show The Letter to Andover
        // Potbello", then "give The Letter to Regent Wilbur Humphrey in
        // Ironfist Castle" — the ids are one past the row, the bank's own
        // 1-based habit.
        // And the chronicle writes itself: type 205's values are the
        // Autonotes rows wording those same stages.
        const bool ok =
            std::find(before.said.begin(), before.said.end(), 435) != before.said.end() &&
            std::find(after.said.begin(), after.said.end(), 427) != after.said.end() &&
            before.ran && state.autonotes.contains(116) && later.autonotes.contains(115);
        if (!beat(ok, "the Seer names Andover first and then Regent Humphrey")) {
            return 1;
        }
    }
    // 8. The letter reaches Humphrey, and this delivery takes it.
    {
        game::WalkState state;
        state.bits.insert(82);
        state.items.push_back(505);
        const auto outcome = game::walk_event(global, 9, state);
        const bool ok =
            outcome.ran && state.gold == 5000 && state.experience == 3000 &&
            state.awards.contains(58) && !state.bits.contains(82) &&
            std::find(state.items.begin(), state.items.end(), 505) == state.items.end() &&
            state.npc_topics.at({4, 0}) == 10;
        if (!beat(ok, "Humphrey pays 5000 and 3000 experience, takes the letter, "
                      "grants award 58")) {
            return 1;
        }
    }
    // 9. Humphrey's little detail: the first council task opens.
    {
        game::WalkState state;
        const auto outcome = game::walk_event(global, 10, state);
        const bool ok = outcome.ran && state.bits.contains(86) && state.npc_topics.at({4, 0}) == 11;
        if (!beat(ok, "his next word sets bit 86 and turns to the shield topic")) {
            return 1;
        }
    }
    // 10. Lord Kilburn's shield closes the first council quest.
    {
        game::WalkState state;
        state.bits.insert(86);
        state.items.push_back(499);
        const auto outcome = game::walk_event(global, 11, state);
        const bool ok =
            outcome.ran && state.gold == 5000 && state.experience == 40000 &&
            state.awards.contains(2) && !state.bits.contains(86) &&
            std::find(state.items.begin(), state.items.end(), 499) == state.items.end() &&
            state.npc_topics.at({4, 0}) == 12;
        if (!beat(ok, "the shield pays 5000 and 40000 experience and grants award 2, "
                      "the first council seal")) {
            return 1;
        }
    }
    // 11. Albert Newton takes the Hourglass of Time.
    {
        game::WalkState state;
        state.items.push_back(433);
        const auto outcome = game::walk_event(global, 52, state);
        const bool ok =
            outcome.ran && state.experience == 50000 && state.awards.contains(3) &&
            std::find(state.items.begin(), state.items.end(), 433) == state.items.end() &&
            state.npc_topics.at({5, 0}) == 54;
        if (!beat(ok, "the Hourglass pays 50000 experience and seals award 3")) {
            return 1;
        }
    }
    // 12. Osric Temper hears the Devil's Post has fallen.
    {
        game::WalkState state;
        state.items.push_back(506);
        const auto outcome = game::walk_event(global, 62, state);
        const bool ok =
            outcome.ran && state.experience == 40000 && state.awards.contains(4) &&
            std::find(state.items.begin(), state.items.end(), 506) == state.items.end() &&
            state.npc_topics.at({6, 0}) == 64;
        if (!beat(ok, "the Devil's Post proof pays 40000 experience and seals award 4")) {
            return 1;
        }
    }
    // 13. Anthony Stone takes the Prince of Thieves off the party's hands:
    //     the check is variable type 214 against 17, and NPCdata.txt row 17
    //     is the Prince himself — the type reads as "this person follows".
    {
        game::WalkState state;
        state.variables[214] = 17;
        const auto outcome = game::walk_event(global, 33, state);
        const bool ok = outcome.ran && state.gold == 10000 && state.experience == 30000 &&
                        state.awards.contains(5) && state.npc_topics.at({16, 0}) == 34;
        if (!beat(ok, "the Prince delivered pays 10000 gold, 30000 experience, award 5")) {
            return 1;
        }
    }
    // 14. Loretta Fleise pays for the stable prices, bit 117's errand.
    {
        game::WalkState state;
        state.bits.insert(117);
        const auto outcome = game::walk_event(global, 80, state);
        const bool ok = outcome.ran && state.gold == 25000 && state.experience == 25000 &&
                        state.awards.contains(6) && state.npc_topics.at({14, 0}) == 81;
        if (!beat(ok, "the stable prices pay 25000 and 25000 experience, award 6")) {
            return 1;
        }
    }
    // 15. Erik Von Stromgard's winter ends the moment he is told.
    {
        game::WalkState state;
        const auto outcome = game::walk_event(global, 89, state);
        const bool ok = outcome.ran && state.experience == 50000 && state.awards.contains(7) &&
                        state.bits.contains(175) && state.npc_topics.at({15, 0}) == 90;
        if (!beat(ok, "ending winter pays 50000 experience and seals award 7")) {
            return 1;
        }
    }
    // 16. The last seal closes the ladder: Humphrey's own event checks the
    //     whole council — award 32, the exposed traitor, among them — and
    //     sets bit 167 when every seal is in.
    {
        game::WalkState state;
        state.items.push_back(499);
        for (const int held : {32, 3, 4, 5, 6, 7}) {
            state.awards.insert(held);
        }
        const auto outcome = game::walk_event(global, 11, state);
        const bool ok = outcome.ran && state.awards.contains(2) && state.bits.contains(167);
        if (!beat(ok, "with every seal and the traitor exposed, bit 167 opens the next act")) {
            return 1;
        }
    }
    // 17. Act two: the letter that names Slicker Silvertongue.
    {
        game::WalkState state;
        state.items.push_back(502);
        const auto outcome = game::walk_event(global, 380, state);
        const bool ok =
            outcome.ran && state.awards.contains(32) && state.bits.contains(168) &&
            std::find(state.items.begin(), state.items.end(), 502) == state.items.end() &&
            state.npc_topics.at({4, 0}) == 103;
        if (!beat(ok, "the proof unseats the traitor: award 32, and bit 168 remembers")) {
            return 1;
        }
    }
    // 18. The Oracle wakes, and the four memory crystals are spent.
    {
        game::WalkState state;
        for (const int crystal : {162, 163, 164, 165}) {
            state.bits.insert(crystal);
        }
        const auto outcome = game::walk_event(global, 76, state);
        const bool ok = outcome.ran && state.awards.contains(33) && state.bits.contains(166) &&
                        !state.bits.contains(162) && !state.bits.contains(165);
        if (!beat(ok, "the Oracle wakes on award 33 and takes the crystals' bits")) {
            return 1;
        }
    }
    // 19. The Control Cube opens the Control Center.
    {
        game::WalkState state;
        state.bits.insert(166);
        state.items.push_back(456);
        const auto outcome = game::walk_event(global, 76, state);
        const bool ok =
            outcome.ran && state.experience == 500000 && state.awards.contains(34) &&
            std::find(state.items.begin(), state.items.end(), 456) == state.items.end() &&
            state.npc_topics.at({8, 0}) == 77;
        if (!beat(ok, "the Cube pays 500000 experience and grants award 34, the Center's key")) {
            return 1;
        }
    }
    // 20. Archibald Ironfist hands over the Ritual of the Void.
    {
        game::WalkState state;
        const auto outcome = game::walk_event(global, 30, state);
        const bool ok = outcome.ran && state.experience == 50000 && state.bits.contains(177) &&
                        std::find(state.items.begin(), state.items.end(), 544) != state.items.end();
        if (!beat(ok, "Archibald gives the Ritual of the Void, item 544, and 50000 experience")) {
            return 1;
        }
    }
    // 21. The reactor refuses whoever comes without the containment.
    {
        world::MapScript hive;
        if (icons.payload("HIVE.EVT", raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, hive) != world::MapScriptError::None) {
            std::cerr << "arc: no HIVE.EVT\n";
            return 1;
        }
        game::WalkState bare;
        bare.bits.insert(202);
        const auto refused = game::walk_event(hive, 60, bare);
        // 22. And yields to the Ritual: the flush takes the scroll, sets
        //     bit 237 and grants award 36, the game's last word.
        game::WalkState ready;
        ready.bits.insert(202);
        ready.items.push_back(544);
        const auto flushed = game::walk_event(hive, 60, ready);
        const bool ok = refused.ran && !bare.awards.contains(36) && flushed.ran &&
                        ready.awards.contains(36) && ready.bits.contains(237) &&
                        std::find(ready.items.begin(), ready.items.end(), 544) == ready.items.end();
        if (!beat(ok, "the flush spends the Ritual for award 36, Destroyed the Hive and "
                      "Saved Enroth")) {
            return 1;
        }
    }
    // 22. A side chain, the template for the rest: Avinril Smythers sets
    //     the hunt for Snergle, and the dwarf king's axe closes it.
    {
        game::WalkState ask;
        const auto sent = game::walk_event(global, 290, ask);
        game::WalkState back;
        back.items.push_back(498);
        const auto paid = game::walk_event(global, 291, back);
        const bool ok = sent.ran && ask.bits.contains(124) && ask.npc_topics.at({32, 0}) == 291 &&
                        paid.ran && back.experience == 20000 && back.awards.contains(37) &&
                        back.bits.contains(27) && back.npc_topics.at({32, 0}) == 207;
        if (!beat(ok, "Snergle's Axe pays 20000 experience and seals award 37, "
                      "Killed Snergle")) {
            return 1;
        }
    }
    // 23. A promotion, walked: the Crusader event checks each member's
    //     class (variable type 2) against the qualifying id and either
    //     promotes — award 8 and the class set to the next rung — or
    //     grants the honorary award 9.
    {
        game::WalkState knight;
        knight.variables[214] = 11;  // the deed's own gate
        knight.variables[2] = 9;
        const auto promoted = game::walk_event(global, 14, knight);
        game::WalkState other;
        other.variables[214] = 11;
        const auto honored = game::walk_event(global, 14, other);
        const bool ok = promoted.ran && knight.awards.contains(8) && knight.variables[2] == 10 &&
                        honored.ran && other.awards.contains(9);
        if (!beat(ok, "the Crusader event promotes class 9 to 10 for award 8, honors the rest")) {
            return 1;
        }
    }
    // 24. And the Wizard's, the same shape on the mage's rungs.
    {
        game::WalkState mage;
        mage.variables[2] = 6;
        const auto promoted = game::walk_event(global, 58, mage);
        const bool ok = promoted.ran && mage.awards.contains(12) && mage.variables[2] == 7 &&
                        mage.experience == 15000;
        if (!beat(ok, "the Wizard event promotes class 6 to 7 for award 12 and 15000 "
                      "experience")) {
            return 1;
        }
    }
    // 25. The obelisk trail: every outdoor map's obelisk writes its own
    //     fragment into the chronicle — Sweet Water's is note 79.
    {
        world::MapScript outa1;
        if (icons.payload("OUTA1.EVT", raw) != lod::LodArchive::PayloadError::None ||
            world::MapScript::parse(raw, outa1) != world::MapScriptError::None) {
            std::cerr << "arc: no OUTA1.EVT\n";
            return 1;
        }
        game::WalkState state;
        const auto outcome = game::walk_event(outa1, 210, state);
        if (!outcome.message || state.autonotes.contains(79)) {
            std::cerr << "arc: the obelisk must wait before writing its fragment\n";
            return 1;
        }
        const auto resumed = game::walk_event(outa1, 210, state, outcome.message->resume_at);
        if (!beat(resumed.ran && !resumed.message && state.autonotes.contains(79),
                  "acknowledging Sweet Water's obelisk writes fragment 79 into the chronicle")) {
            return 1;
        }
    }
    // 26. The dark turn-in: the Zenofex letter can be shown to the wrong
    //     man. Slicker's own event swaps bit 200 for 201 — the traitor
    //     warned — and keeps the letter in your hands; a bare visit hands
    //     you a Cloak of Baa instead.
    {
        game::WalkState warned;
        warned.bits.insert(200);
        warned.items.push_back(502);
        const auto shown = game::walk_event(global, 102, warned);
        game::WalkState bare;
        const auto given = game::walk_event(global, 102, bare);
        const bool ok =
            shown.ran && !warned.bits.contains(200) && warned.bits.contains(201) &&
            std::find(warned.items.begin(), warned.items.end(), 502) != warned.items.end() &&
            given.ran && std::find(bare.items.begin(), bare.items.end(), 485) != bare.items.end();
        if (!beat(ok, "showing Slicker the letter tips the traitor; empty hands get a Cloak "
                      "of Baa")) {
            return 1;
        }
    }
    // 27..30. Four side chains the ledger points at, walked the way
    //         Snergle's was: each takes its token and pays its own purse.
    {
        struct SideQuest {
            std::uint16_t event;
            int item;
            int award;
            int gold;
            int experience;
            const char* what;
        };
        // The Candelabra (449), Andrew's Harp (479) and the Pearl of
        // Putrescence (458), each read off its own event's gives.
        static constexpr std::array<SideQuest, 3> kFetches{
            {{297, 449, 39, 1000, 2000, "the Candelabra pays 1000 gold, 2000 experience, award 39"},
             {304, 479, 40, 5000, 10000,
              "Andrew's Harp pays 5000 gold, 10000 experience, award 40"},
             {341, 458, 51, 0, 5000,
              "the Pearl of Putrescence pays 5000 experience and seals award 51"}}};
        for (const auto& quest : kFetches) {
            game::WalkState state;
            state.items.push_back(quest.item);
            const auto outcome = game::walk_event(global, quest.event, state);
            const bool ok =
                outcome.ran && state.awards.contains(quest.award) && state.gold == quest.gold &&
                state.experience == quest.experience &&
                std::find(state.items.begin(), state.items.end(), quest.item) == state.items.end();
            if (!beat(ok, quest.what)) {
                return 1;
            }
        }
        // The Wicked Crystal is a bit, not a token: bit 21 held earns
        // award 45 and the Mist mayor's own reward.
        game::WalkState crystal;
        crystal.bits.insert(21);
        const auto outcome = game::walk_event(global, 316, crystal);
        if (!beat(outcome.ran && crystal.awards.contains(45) && crystal.gold == 3000 &&
                      crystal.experience == 10000,
                  "the Wicked Crystal destroyed pays 3000 gold, 10000 experience, award 45")) {
            return 1;
        }
    }
    // 31..34. The other four ladders, so no class's promotion breaks
    //         unnoticed: each promotes its qualifying class one rung and
    //         hands the honorary award to everyone else.
    {
        struct Ladder {
            std::uint16_t event;
            int token;       // the deed's proof, where the lord asks for one
            int qualifying;  // the class the check names
            int promoted;    // the rung it becomes
            int award;       // the promotion's own honor
            int honorary;    // and the honor for those who cannot take it
            const char* what;
        };
        static constexpr std::array<Ladder, 4> kLadders{
            {{16, 455, 10, 11, 10, 11,
              "the Dragon Claw ladder promotes class 10 to 11 for award 10, honours with 11"},
             {60, 457, 7, 8, 14, 15,
              "the Crystal of Terrax ladder promotes class 7 to 8 for award 14"},
             {69, 0, 0, 1, 16, 17, "the fifth lord's ladder promotes class 0 to 1 for award 16"},
             {71, 508, 1, 2, 18, 19,
              "the Discharge Papers ladder promotes class 1 to 2 for award 18"}}};
        for (const auto& ladder : kLadders) {
            game::WalkState able;
            able.variables[214] = 11;
            if (ladder.token != 0) {
                able.items.push_back(ladder.token);
            }
            able.variables[2] = ladder.qualifying;
            const auto promoted = game::walk_event(global, ladder.event, able);
            game::WalkState other;
            other.variables[214] = 11;
            if (ladder.token != 0) {
                other.items.push_back(ladder.token);
            }
            other.variables[2] = ladder.qualifying + 500;  // a class that cannot
            const auto honored = game::walk_event(global, ladder.event, other);
            const bool ok = promoted.ran && able.awards.contains(ladder.award) &&
                            able.variables[2] == ladder.promoted && honored.ran &&
                            other.awards.contains(ladder.honorary);
            if (!beat(ok, ladder.what)) {
                return 1;
            }
        }
    }
    std::cout << passed << " beats of the opening arc hold\n";
    return 0;
}

}  // namespace

// Do the actor's three unwritten timers ever arrive non-zero from a file?
//
// The executable copies the whole actor array in and out as a straight image
// of the 548-byte record — `memcpy(0x56f478, buffer, 548 * count)` at
// 0x46dc92 and 0x48c0a6, and `fwrite(0x56f478, 548, count)` at 0x46cd92 —
// so whatever the map's own actor block holds at `+0xf4`, `+0x114` and
// `+0x124` is what the AI reads there. This walks every map's block and
// counts.
int do_actor_timers(const starhaven::lod::LodArchive& icons) {
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    (void)icons;

    lod::GameLodArchive games;
    if (lod::GameLodArchive::open(*starhaven::platform::install_from_env() / "data" / "Games.lod",
                                  games) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    struct Field {
        std::size_t offset;
        const char* name;
        std::size_t nonzero = 0;
    };
    std::array<Field, 4> fields{
        {{0xf4, "+0xf4"}, {0x104, "+0x104"}, {0x114, "+0x114"}, {0x124, "+0x124"}}};
    std::size_t maps = 0;
    std::size_t actors = 0;
    for (const auto& entry : games.entries()) {
        const std::string name = entry.name;
        const bool outdoor = name.size() > 4 && name.compare(name.size() - 4, 4, ".ddm") == 0;
        const bool indoor = name.size() > 4 && name.compare(name.size() - 4, 4, ".dlv") == 0;
        if (!outdoor && !indoor) {
            continue;
        }
        std::span<const std::byte> raw;
        world::MapEventFile file;
        if (games.payload(name, raw) != lod::GameLodArchive::PayloadError::None ||
            world::parse_map_event(raw, file) != world::MapEventError::None) {
            continue;
        }
        world::EventLayout layout;
        if (world::parse_event_layout(file, layout) != world::EventLayoutError::None) {
            continue;
        }
        const std::size_t offset = layout.actors_offset;
        const std::uint32_t count = layout.actor_count;
        ++maps;
        const auto& bytes = file.payload;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t base = offset + i * world::kActorRecordSize;
            if (base + world::kActorRecordSize > bytes.size()) {
                break;
            }
            ++actors;
            for (auto& field : fields) {
                bool any = false;
                for (std::size_t b = 0; b < 8; ++b) {
                    any = any || bytes[base + field.offset + b] != 0;
                }
                field.nonzero += any ? 1 : 0;
            }
        }
    }
    std::cout << maps << " maps, " << actors << " actors on disk\n";
    for (const auto& field : fields) {
        std::cout << "  " << field.name << ": " << field.nonzero << " non-zero ("
                  << (actors > 0 ? 100 * field.nonzero / actors : 0) << "%)\n";
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }
    if (std::string_view(argv[1]) == "--coverage" && argc == 3 &&
        std::string_view(argv[2]) != "--strict") {
        print_usage(argv[0]);
        return 2;
    }
    namespace lod = starhaven::lod;
    namespace world = starhaven::world;

    const auto install = starhaven::platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << starhaven::platform::kInstallEnvVar << "\n";
        return 1;
    }
    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return 1;
    }

    const std::string stem = argv[1];
    if (stem == "--object-contacts") {
        if (!install) {
            std::cerr << "error: game installation not found\n";
            return 1;
        }
        return do_object_contacts(*install / "data");
    }
    if (stem == "--object-actor") {
        if (!install) {
            std::cerr << "error: game installation not found\n";
            return 1;
        }
        return do_object_actor(*install / "data");
    }
    if (stem == "--object-removal") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_removal(*install / "data");
    }
    if (stem == "--object-expiry") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_expiry(*install / "data");
    }
    if (stem == "--object-impact") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_impact(*install / "data");
    }
    if (stem == "--object-loot") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_loot(*install / "data");
    }
    if (stem == "--object-lifecycle") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_lifecycle(*install / "data");
    }
    if (stem == "--object-spawns") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_object_spawns(icons, *install / "data");
    }
    if (stem == "--face-bits") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_face_bits(icons, *install / "data");
    }
    if (stem == "--decoration-events") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_decoration_events(icons, *install / "data");
    }
    if (stem == "--generated-items") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_generated_items(icons, *install / "data");
    }
    if (stem == "--messages") {
        if (argc != 2) {
            print_usage(argv[0]);
            return 2;
        }
        return do_messages(icons, *install / "data");
    }
    if (stem == "--decorations") {
        return do_decorations(icons, *install / "data");
    }
    if (stem == "--coverage") {
        const auto coverage = starhaven::game::audit_script_coverage(icons);
        starhaven::game::write_script_coverage(coverage, std::cout);
        if (!coverage.complete()) {
            std::cerr << "error: incomplete script audit; see SCRIPT rows (or no .EVT entries)\n";
            return 1;
        }
        return argc == 3 && coverage.has_gaps() ? 1 : 0;
    }
    if (stem == "--scan") {
        return do_scan(icons);
    }
    if (stem == "--transitions") {
        return do_transitions(icons, *install / "data");
    }
    if (stem == "--ledger") {
        const bool walk = argc == 3 && std::string(argv[2]) == "walk";
        return do_ledger(icons, *install / "data", walk);
    }
    if (stem == "--asks") {
        return do_asks(icons);
    }
    if (stem == "--currencies") {
        return do_currencies(icons, *install / "data");
    }
    if (stem == "--arc") {
        return do_arc(icons, *install / "data");
    }
    if (stem == "--launches") {
        return do_launches(icons);
    }
    if (stem == "--projectiles") {
        return do_projectiles(icons);
    }
    if (stem == "--catalog" && argc == 3) {
        return do_catalog(icons, std::atoi(argv[2]));
    }
    if (stem == "--npc-mutations") {
        return do_npc_mutations(icons, *install / "data");
    }
    if (stem == "--actor-timers") {
        return do_actor_timers(icons);
    }
    if (stem == "--headers") {
        return do_headers(icons);
    }
    if (stem == "--soundsweep") {
        return do_soundsweep(icons);
    }
    if (stem == "--unheaded") {
        return do_unheaded(icons);
    }
    if (stem == "--textures") {
        lod::LodArchive bitmaps;
        if (lod::LodArchive::open(*install / "data" / "BITMAPS.LOD", bitmaps) !=
            lod::LodError::None) {
            std::cerr << "error: could not open BITMAPS.LOD\n";
            return 1;
        }
        return do_sounds(icons, bitmaps);
    }
    if (stem == "--out") {
        return do_out(icons, argc == 3 ? std::string(argv[2]) + ".EVT" : "OUT.EVT");
    }
    if (stem == "--variables") {
        return do_variables(icons);
    }
    std::span<const std::byte> raw;
    world::MapScript script;
    if (icons.payload(stem + ".EVT", raw) != lod::LodArchive::PayloadError::None ||
        world::MapScript::parse(raw, script) != world::MapScriptError::None) {
        std::cerr << "error: no script for " << stem << "\n";
        return 1;
    }
    world::MapStrings strings;
    if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
        (void)world::MapStrings::parse(raw, strings);
    }

    if (argc == 3) {
        const auto id = static_cast<std::uint16_t>(std::atoi(argv[2]));
        const auto steps = script.event(id);
        if (steps.empty()) {
            std::cerr << "error: " << stem << " has no event " << id << "\n";
            return 1;
        }
        std::cout << "event " << id << ", " << steps.size() << " steps\n";
        for (const auto& step : steps) {
            std::cout << "  " << static_cast<int>(step.sequence) << ": opcode "
                      << static_cast<int>(step.opcode) << " (" << step.arguments.size()
                      << " bytes)";
            for (const std::uint8_t b : step.arguments) {
                std::cout << " " << std::hex << static_cast<int>(b) << std::dec;
            }
            // The three opcodes whose first argument is a string.
            if (world::names_a_string(step.opcode) && !step.arguments.empty()) {
                std::cout << "  \x22" << strings.at(step.arguments.front()) << "\x22";
            }
            std::cout << "\n";
        }
        return 0;
    }

    std::size_t events = 0;
    std::uint16_t last = 0;
    for (const auto& step : script.steps()) {
        if (step.event_id != last) {
            ++events;
            last = step.event_id;
        }
    }
    std::cout << stem << ": " << script.size() << " steps in " << events << " events, "
              << strings.size() << " strings\n";
    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (!strings.at(i).empty()) {
            std::cout << "  [" << i << "] " << strings.at(i) << "\n";
        }
    }
    return 0;
}
