#include "core/world/map_session.hpp"

#include "core/random.hpp"
#include "core/world/monster_spawn.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <span>

#include "core/data/building_stats.hpp"
#include "core/data/game_data.hpp"
#include "core/data/monster_stats.hpp"
#include "core/data/npc_stats.hpp"
#include "core/image/bitmap.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/world/map_event.hpp"
#include "core/world/tile_table.hpp"

namespace starhaven::world {

namespace {

bool ends_with_ignoring_case(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = static_cast<unsigned char>(text[offset + i]);
        const auto b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

std::string stem_of(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

// Load the global tables every map resolves its placed things through. A
// missing table is not fatal: the map still loads, with less resolved.
void load_tables(const std::filesystem::path& data_dir, MapSession& out) {
    lod::LodArchive icons;
    if (lod::LodArchive::open(data_dir / "icons.lod", icons) != lod::LodError::None) {
        return;
    }
    std::span<const std::byte> raw;
    auto load = [&](const char* name, auto& table, auto parse) {
        if (icons.payload(name, raw) == lod::LodArchive::PayloadError::None) {
            if (parse(raw, table) != decltype(parse(raw, table)){}) {
                table = std::remove_reference_t<decltype(table)>{};
            }
        }
    };
    load("DSFT.BIN", out.sprite_frames, SpriteFrameTable::parse);
    load("DSOUNDS.BIN", out.sounds, SoundTable::parse);
    load("DOBJLIST.BIN", out.object_descriptors, ObjectTable::parse);
    load("DDECLIST.BIN", out.decoration_types, DecorationTable::parse);
    load("DMONLIST.BIN", out.monsters, MonsterList::parse);
}

// A map's script and strings, which are named after the map rather than fixed.
void load_script(const std::filesystem::path& data_dir, const std::string& stem, MapSession& out) {
    lod::LodArchive icons;
    if (lod::LodArchive::open(data_dir / "icons.lod", icons) != lod::LodError::None) {
        return;
    }
    std::span<const std::byte> raw;
    if (icons.payload(stem + ".EVT", raw) == lod::LodArchive::PayloadError::None) {
        if (MapScript::parse(raw, out.script) != MapScriptError::None) {
            out.script = MapScript{};
        }
    }
    if (icons.payload(stem + ".STR", raw) == lod::LodArchive::PayloadError::None) {
        if (MapStrings::parse(raw, out.script_strings) != MapScriptError::None) {
            out.script_strings = MapStrings{};
        }
    }
}

// Ground textures for the tile indices this map actually uses, resolved
// through DTILE.BIN (see docs/formats/dtile.md).
int load_ground_tiles(const std::filesystem::path& data_dir, const OdmTerrain& terrain,
                      render::TileSet& out) {
    lod::LodArchive icons;
    lod::LodArchive bitmaps;
    if (lod::LodArchive::open(data_dir / "icons.lod", icons) != lod::LodError::None ||
        lod::LodArchive::open(data_dir / "BITMAPS.LOD", bitmaps) != lod::LodError::None) {
        return -1;
    }
    std::span<const std::byte> dtile;
    if (icons.payload("DTILE.BIN", dtile) != lod::LodArchive::PayloadError::None) {
        return -1;
    }
    TileTable table;
    if (TileTable::parse(dtile, table) != TileTableError::None) {
        return -1;
    }

    std::array<bool, 256> used{};
    for (const std::uint8_t t : terrain.tilemap) {
        used[t] = true;
    }

    int resolved = 0;
    for (int i = 0; i < 256; ++i) {
        if (!used[static_cast<std::size_t>(i)]) {
            continue;
        }
        const auto* record = table.at(static_cast<std::uint8_t>(i));
        // An empty name is a reserved slot, not an error: the shipped table
        // has more rows than art.
        if (record == nullptr || record->name.empty()) {
            continue;
        }
        // The tile set owns its textures, so decode straight into it rather
        // than copying out of the shared cache.
        std::span<const std::byte> raw;
        if (bitmaps.payload(record->name, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        image::Bitmap bmp;
        if (image::decode_bitmap(raw, bmp) != image::BitmapError::None) {
            continue;
        }
        render::Texture texture;
        if (!render::Texture::create(bmp.width, bmp.height, std::move(bmp.rgba), texture)) {
            continue;
        }
        if (out.set(static_cast<std::uint8_t>(i), std::move(texture))) {
            ++resolved;
        }
    }
    return resolved;
}

// Does this installation have art for an animation? The sprite frame table
// names the entries, so ask it rather than guessing at a view digit.
bool drawable(const SpriteFrameTable& frames, assets::AssetCache& cache,
              const std::string& animation) {
    if (animation.empty()) {
        return false;
    }
    const auto group = frames.group(animation);
    if (group.empty()) {
        return cache.has_sprite(animation);
    }
    return cache.has_sprite(SpriteFrameTable::sprite_entry(group.front(), 0));
}

// An actor record's monster id is 1-based (see docs/formats/event-actors.md).
// Monsters come in A/B/C triples and the variants share the A variant's art,
// so a missing animation falls back to the triple's first.
std::string actor_animation(const MapSession& session, assets::AssetCache& cache, int monster_id) {
    if (monster_id <= 0) {
        return {};
    }
    const auto* entry = session.monsters.at(static_cast<std::size_t>(monster_id - 1));
    if (entry == nullptr) {
        return {};
    }
    const std::string& animation = entry->animation(MonsterAnimation::Stand);
    if (drawable(session.sprite_frames, cache, animation)) {
        return animation;
    }
    const auto a_index = static_cast<std::size_t>(monster_id - 1 - ((monster_id - 1) % 3));
    if (const auto* a = session.monsters.at(a_index); a != nullptr) {
        const std::string& alt = a->animation(MonsterAnimation::Stand);
        if (drawable(session.sprite_frames, cache, alt)) {
            return alt;
        }
    }
    return {};
}

// The monsters an outdoor map's own spawn points call for. Each point names an
// encounter slot, the slot names a monster and how many of it appear, and the
// group is spread around the point at whatever height the terrain is.
void spawn_from_points(const std::filesystem::path& data_dir, const data::MapStatsEntry& stats,
                       assets::AssetCache& cache, MapSession& out) {
    if (out.monster_spawns.empty()) {
        return;
    }
    data::TextTable text;
    data::MonsterStatsTable monsters;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", text) != data::GameDataError::None ||
        data::MonsterStatsTable::parse(text, monsters) != data::MonsterStatsError::None) {
        return;
    }
    out.map_id = stats.id;
    out.encounters = stats.monsters;
    out.placed_actor_count = out.actors.size();

    // Seeded from the map's own row so a map always populates the same way:
    // screenshots, benchmarks and bug reports all depend on it.
    respawn_monsters(monsters, cache, static_cast<std::uint32_t>(stats.id) * 2654435761U, out);
}

}  // namespace

void rebuild_indoor_collision(MapSession& out) {
    out.collision = {};
    std::vector<render::Vec3> corners;
    for (const auto& f : out.blv.faces) {
        if (f.invisible() || f.vertex_count < 3) {
            continue;
        }
        corners.clear();
        for (std::size_t k = 0; k < f.vertex_count; ++k) {
            const auto& v = out.blv.vertices[f.vertex_ids[k]];
            corners.push_back(to_render_space(v.x, v.y, v.z));
        }
        out.collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
    }
}

void respawn_monsters(const data::MonsterStatsTable& monsters, assets::AssetCache& cache,
                      std::uint32_t seed, MapSession& out) {
    if (out.monster_spawns.empty() || out.placed_actor_count > out.actors.size()) {
        return;
    }
    out.actors.resize(out.placed_actor_count);
    Mm6Random random{seed};

    for (const auto& point : out.monster_spawns) {
        const int slot = encounter_slot_of(point.index);
        if (slot < 0 || static_cast<std::size_t>(slot) >= out.encounters.size()) {
            continue;
        }
        const auto& encounter = out.encounters[static_cast<std::size_t>(slot)];
        const int monster_id = encounter_monster_id(monsters, encounter);
        const SpawnCount count = parse_spawn_count(encounter.count);
        if (monster_id <= 0 || count.empty()) {
            continue;
        }
        std::string animation = actor_animation(out, cache, monster_id);
        if (animation.empty()) {
            continue;
        }
        const auto& rows = monsters.entries();
        const auto index = static_cast<std::size_t>(monster_id - 1);
        const std::string name =
            index < rows.size() ? data::cp1252_to_utf8(rows[index].name) : std::string{};

        const int span = count.high - count.low + 1;
        const int group = count.low + static_cast<int>(random.next() % static_cast<unsigned>(span));
        for (int i = 0; i < group; ++i) {
            const auto [dx, dy] = spawn_offset(i, group);
            const float x = static_cast<float>(point.x) + dx;
            const float y = static_cast<float>(point.y) + dy;
            // The point's own height is zero on most of them; the ground is
            // what the map means. See docs/formats/odm-tile-index.md.
            const render::Vec3 position =
                to_render_space(static_cast<int>(x), static_cast<int>(y), point.z);
            out.actors.push_back(
                {animation,
                 name,
                 monster_id,
                 {position.x, out.terrain_height_at(position.x, position.z), position.z}});
        }
    }
}

namespace {

// Actors and loot come from the map's event file, whichever kind it is. Their
// names come from the design tables: a monster id is a 1-based MONSTERS.TXT
// row, and a contained item id indexes ITEMS.TXT directly.
void load_placed_things(const lod::GameLodArchive& archive, const std::filesystem::path& data_dir,
                        const std::string& map_name, assets::AssetCache& cache, MapSession& out) {
    const std::string stem = stem_of(map_name);
    const std::string extension = out.outdoor() ? ".ddm" : ".dlv";
    std::span<const std::byte> entry;
    MapEventFile file;
    if (archive.payload(stem + extension, entry) != lod::GameLodArchive::PayloadError::None ||
        parse_map_event(entry, file) != MapEventError::None) {
        return;
    }
    if (out.indoor()) {
        out.doors = extract_doors(file);
    }
    data::TextTable monster_text;
    data::MonsterStatsTable monster_stats;
    if (data::load_text_table(data_dir, "MONSTERS.TXT", monster_text) ==
        data::GameDataError::None) {
        (void)data::MonsterStatsTable::parse(monster_text, monster_stats);
    }
    data::ItemStatsTable items;
    (void)data::load_item_stats(data_dir, items);

    for (const auto& actor : extract_actors(file)) {
        std::string animation = actor_animation(out, cache, actor.monster_id);
        if (animation.empty()) {
            continue;
        }
        // An indoor unique carries its own name over a base monster's row, so
        // prefer the record's name when it has one.
        std::string name = actor.name;
        if (name.empty() && actor.monster_id > 0 &&
            actor.monster_id <= monster_stats.entries().size()) {
            name = monster_stats.entries()[actor.monster_id - 1].name;
        }
        out.actors.push_back({std::move(animation), data::cp1252_to_utf8(name), actor.monster_id,
                              to_render_space(actor.x, actor.y, actor.z)});
    }
    for (const auto& object : extract_sprite_objects(file)) {
        std::string name;
        if (const std::int32_t id = object.contained_item.item_id; id > 0) {
            if (const auto* item = items.at(static_cast<std::size_t>(id)); item != nullptr) {
                name = data::cp1252_to_utf8(item->name);
            }
        }
        out.objects.push_back(
            {object.descriptor_index, std::move(name), object.contained_item.item_id,
             to_render_space(static_cast<int>(object.x), static_cast<int>(object.y),
                             static_cast<int>(object.z))});
    }
}

MapSessionError load_outdoor(std::span<const std::byte> entry,
                             const std::filesystem::path& data_dir, MapSession& out) {
    if (parse_odm_terrain(entry, out.odm, out.terrain) != OdmError::None) {
        return MapSessionError::BadMap;
    }
    out.terrain_mesh = render::build_terrain_mesh(out.terrain, {});
    if (load_ground_tiles(data_dir, out.terrain, out.tiles) <= 0) {
        out.tiles = render::TileSet::make_placeholder();
    }
    if (extract_models(out.odm, out.models) != OdmError::None) {
        out.models.clear();
    }
    if (extract_model_meshes(out.odm, out.meshes) != OdmError::None) {
        out.meshes.clear();
    }

    std::vector<OdmDecoration> decorations;
    if (extract_decorations(out.odm, decorations) != OdmError::None) {
        decorations.clear();
    }
    for (const auto& d : decorations) {
        const auto* type = out.decoration_types.at(d.kind);
        out.decorations.push_back({d.name, to_render_space(d.x, d.y, d.z),
                                   type == nullptr ? std::uint16_t{0} : type->sound_id,
                                   type == nullptr ? std::uint16_t{0} : type->radius});
    }

    // The map's own index of what stands near each tile, and where it spawns
    // monsters. Both are optional: a map that lost its tail still loads.
    if (extract_tile_index(out.odm, out.tile_index) != OdmError::None) {
        out.tile_index.entries.clear();
        out.tile_index.starts.clear();
    }
    if (extract_spawn_points(out.odm, out.monster_spawns) != OdmError::None) {
        out.monster_spawns.clear();
    }

    // Collision: the models' own facets. Terrain is sampled instead, which is
    // both cheaper and exactly right.
    std::vector<render::Vec3> corners;
    for (const auto& m : out.meshes) {
        for (const auto& f : m.facets) {
            if (f.vertex_count < 3) {
                continue;
            }
            corners.clear();
            for (std::size_t k = 0; k < f.vertex_count; ++k) {
                const auto& v = m.vertices[f.vertex_ids[k]];
                corners.push_back(to_render_space(v.x, v.y, v.z));
            }
            out.collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
        }
    }
    return MapSessionError::None;
}

// Where to put the player when nothing else says. Indoors the level names its
// own start; the centre of the bounding box is usually inside solid rock.
render::Vec3 indoor_spawn(const BlvMap& map, const std::vector<SessionDecoration>& decorations) {
    for (const auto& d : decorations) {
        if (d.name == "Party Start") {
            return d.position;
        }
    }
    const BlvFace* best = nullptr;
    long best_area = -1;
    for (const auto& f : map.faces) {
        if (f.invisible() || f.vertex_count < 3 || f.nz() < 0.9f) {
            continue;
        }
        int minx = 0, maxx = 0, miny = 0, maxy = 0;
        for (std::size_t k = 0; k < f.vertex_count; ++k) {
            const auto& v = map.vertices[f.vertex_ids[k]];
            if (k == 0) {
                minx = maxx = v.x;
                miny = maxy = v.y;
            }
            minx = std::min<int>(minx, v.x);
            maxx = std::max<int>(maxx, v.x);
            miny = std::min<int>(miny, v.y);
            maxy = std::max<int>(maxy, v.y);
        }
        const long area = static_cast<long>(maxx - minx) * (maxy - miny);
        if (area > best_area) {
            best_area = area;
            best = &f;
        }
    }
    if (best == nullptr) {
        return {};
    }
    long sx = 0, sy = 0, sz = 0;
    for (std::size_t k = 0; k < best->vertex_count; ++k) {
        const auto& v = map.vertices[best->vertex_ids[k]];
        sx += v.x;
        sy += v.y;
        sz += v.z;
    }
    const int n = best->vertex_count;
    return to_render_space(static_cast<int>(sx / n), static_cast<int>(sy / n),
                           static_cast<int>(sz / n));
}

MapSessionError load_indoor(std::span<const std::byte> entry, MapSession& out) {
    if (parse_blv(entry, out.blv) != BlvError::None) {
        return MapSessionError::BadMap;
    }

    // Collision uses the same faces the renderer draws, minus the portals.
    rebuild_indoor_collision(out);

    // Indoor decorations carry a name and no type id, so the decoration table
    // is entered by name.
    for (const auto& d : find_decorations(out.blv)) {
        const auto* type = out.decoration_types.find(d.name);
        out.decorations.push_back({d.name, to_render_space(d.x, d.y, d.z),
                                   type == nullptr ? std::uint16_t{0} : type->sound_id,
                                   type == nullptr ? std::uint16_t{0} : type->radius});
    }
    out.spawn = indoor_spawn(out.blv, out.decorations);
    return MapSessionError::None;
}

}  // namespace

std::vector<std::size_t> MapSession::decorations_near(float x, float z) const {
    std::vector<std::size_t> out;
    decorations_near(x, z, out);
    return out;
}

void MapSession::decorations_near(float x, float z, std::vector<std::size_t>& out) const {
    out.clear();
    if (!outdoor()) {
        return;
    }
    // The renderer's z is the map's y; see to_render_space.
    const auto run = tile_index.at(OdmTileIndex::tile_x_of(x), OdmTileIndex::tile_y_of(z));
    for (const std::uint16_t pid : run) {
        if (pid_type(pid) != kPidDecoration) {
            continue;
        }
        const std::size_t id = pid_id(pid);
        if (id < decorations.size()) {
            out.push_back(id);
        }
    }
}

float MapSession::terrain_height_at(float x, float z) const {
    if (!outdoor()) {
        return 0.0f;
    }
    constexpr int dim = OdmTerrain::kGridDim;
    const render::TerrainScale scale{};
    const float half = (dim - 1) * scale.cell_size * 0.5f;
    const float gx = (x + half) / scale.cell_size;
    const float gz = (z + half) / scale.cell_size;

    const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, dim - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(gz)), 0, dim - 1);
    const int x1 = std::min(x0 + 1, dim - 1);
    const int z1 = std::min(z0 + 1, dim - 1);
    const float fx = std::clamp(gx - static_cast<float>(x0), 0.0f, 1.0f);
    const float fz = std::clamp(gz - static_cast<float>(z0), 0.0f, 1.0f);

    auto at = [&](int cx, int cz) {
        const std::size_t index = static_cast<std::size_t>(cz) * dim + cx;
        return static_cast<float>(terrain.heightmap[index]) * scale.height_scale;
    };
    const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * fx;
    const float bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * fx;
    return top + (bottom - top) * fz;
}

MapSessionError load_map_session(const std::filesystem::path& games_lod,
                                 const std::filesystem::path& data_dir, std::string_view map_name,
                                 assets::AssetCache& cache, MapSession& out) {
    out = MapSession{};
    out.file_name = std::string(map_name);

    if (ends_with_ignoring_case(map_name, ".odm")) {
        out.kind = MapKind::Outdoor;
    } else if (ends_with_ignoring_case(map_name, ".blv")) {
        out.kind = MapKind::Indoor;
    } else {
        return MapSessionError::UnknownKind;
    }

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(games_lod, archive) != lod::GameLodError::None) {
        return MapSessionError::NoArchive;
    }
    std::span<const std::byte> entry;
    if (archive.payload(map_name, entry) != lod::GameLodArchive::PayloadError::None) {
        return MapSessionError::NotFound;
    }

    load_tables(data_dir, out);

    // The design table names the map and picks its music. A level's own header
    // carries a name too, but it is often a placeholder the designers never
    // filled in: D01.blv calls itself "No Name Level" where the table calls it
    // "Goblinwatch". See docs/formats/text-tables.md.
    data::MapStatsTable maps;
    if (data::load_map_stats(data_dir, maps) == data::GameDataError::None) {
        if (const auto* stats = maps.find(map_name); stats != nullptr) {
            out.display_name = data::cp1252_to_utf8(stats->name);
            out.music_track = stats->music_track;
            out.refill_days = stats->refill_days;
            out.treasure_level = stats->treasure_level;
        }
    }

    // The establishments the design table places here. Only outdoor maps have
    // a code it references (see docs/formats/text-tables.md).
    if (const std::string code = data::map_code_of(out.file_name); !code.empty()) {
        data::BuildingStatsTable buildings;
        if (data::load_building_stats(data_dir, buildings) == data::GameDataError::None) {
            data::NpcTable npcs;
            data::NpcProfessionTable professions;
            data::NpcDialogueTable dialogue;
            (void)data::load_npcs(data_dir, npcs);
            (void)data::load_npc_professions(data_dir, professions);
            (void)data::load_npc_dialogue(data_dir, dialogue);

            for (const auto* b : buildings.on_map(code)) {
                SessionBuilding entry{data::cp1252_to_utf8(b->name),
                                      b->type,
                                      data::cp1252_to_utf8(b->proprietor),
                                      b->opens,
                                      b->closes,
                                      {}};
                for (const auto* n : npcs.in_building(b->id)) {
                    std::string who = data::cp1252_to_utf8(n->name);
                    if (const auto* p = professions.at(n->profession_id); p != nullptr) {
                        who += ", " + p->name;
                    }
                    // What they can be asked about, from their event columns.
                    std::string topics;
                    for (const int event : n->events) {
                        const auto* said = dialogue.at(event);
                        if (said == nullptr) {
                            continue;
                        }
                        if (!topics.empty()) {
                            topics += "/";
                        }
                        topics += data::cp1252_to_utf8(said->topic);
                    }
                    if (!topics.empty()) {
                        who += " (" + topics + ")";
                    }
                    entry.occupants.push_back(std::move(who));
                    entry.occupant_professions.push_back(n->profession_id);

                    SessionNpc person;
                    person.name = data::cp1252_to_utf8(n->name);
                    person.npc_id = n->id;
                    person.profession_id = n->profession_id;
                    if (const auto* p = professions.at(n->profession_id); p != nullptr) {
                        person.profession = p->name;
                        person.personality = p->personality;
                    }
                    for (std::size_t k = 0; k < person.topics.size() && k < n->events.size(); ++k) {
                        person.topics[k] = n->events[k];
                    }
                    entry.people.push_back(std::move(person));
                }
                out.buildings.push_back(std::move(entry));
            }
        }
    }

    const MapSessionError e =
        out.outdoor() ? load_outdoor(entry, data_dir, out) : load_indoor(entry, out);
    if (e != MapSessionError::None) {
        return e;
    }

    load_script(data_dir, stem_of(out.file_name), out);
    load_placed_things(archive, data_dir, out.file_name, cache, out);

    // An outdoor map's event file holds the actors the designers placed —
    // townspeople, mostly, and on nine of the fifteen maps none at all. The
    // wandering monsters are not in it: the map ships spawn points and the
    // design table says what appears at them. Both are placed, because
    // nothing here writes the event file back.
    if (out.outdoor()) {
        if (const auto* stats = maps.find(out.file_name); stats != nullptr) {
            spawn_from_points(data_dir, *stats, cache, out);
        }
    }
    return MapSessionError::None;
}

}  // namespace starhaven::world
