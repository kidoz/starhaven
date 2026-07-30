#ifndef STARHAVEN_CORE_WORLD_MAP_SESSION_HPP
#define STARHAVEN_CORE_WORLD_MAP_SESSION_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/assets/asset_cache.hpp"
#include "core/data/map_stats.hpp"
#include "core/data/monster_stats.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/render/math3d.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/tile_set.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/collision.hpp"
#include "core/world/decoration_table.hpp"
#include "core/world/map_event.hpp"
#include "core/world/map_script.hpp"
#include "core/world/monster_list.hpp"
#include "core/world/object_table.hpp"
#include "core/world/odm_map.hpp"
#include "core/world/sound_table.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace starhaven::world {

enum class MapKind : std::uint8_t {
    Unknown,
    Outdoor,  // .odm — a terrain heightfield with placed models
    Indoor,   // .blv — a closed level built from faces
};

// One placed decoration, with everything a renderer or a mixer needs, so that
// neither has to know which kind of map it came from.
struct SessionDecoration {
    std::string name;  // also the sprite frame table's animation name
    render::Vec3 position;
    std::uint16_t sound_id = 0;  // 0 when the decoration is silent
    std::uint16_t radius = 0;    // how much room it takes; see DecorationTable
};

// One monster standing on the map, already resolved to a drawable animation.
struct SessionActor {
    std::string animation;  // empty when this install has no art for it
    std::string name;       // the MONSTERS.TXT name for the actor's monster id
    int monster_id = 0;     // 1-based; indexes MONSTERS.TXT directly
    render::Vec3 position;
};

// One establishment the design table places on this map: a shop, temple,
// tavern or guild. They have no position in the world — the table says which
// map they belong to and nothing more.
// One person the NPC table places in an establishment, with the pieces a
// conversation needs rather than a line of display text.
struct SessionNpc {
    std::string name;
    int npc_id = 0;               // the 1-based NPCdata.txt row, for mutations
    int profession_id = 0;        // indexes npcprof.txt, and PROFTEXT.txt
    std::string profession;       // its name, for showing
    std::string personality;      // the profession's, which npcbtb describes
    std::array<int, 3> topics{};  // npctopic/npctext ids from the event columns
};

struct SessionBuilding {
    std::string name;
    std::string type;
    std::string proprietor;
    int building_id = 0;  // the 1-based 2DEvents.txt row
    int opens = 0;
    int closes = 0;

    // The people the NPC table places inside, as "Name, Profession", and the
    // `npcprof.txt` id of each — which is what `PROFTEXT.txt` is keyed by, so
    // an occupant can be asked what their trade says today.
    std::vector<std::string> occupants;
    std::vector<int> occupant_professions;

    // And the same people with their parts kept apart, for talking to.
    std::vector<SessionNpc> people;
};

// One loot object or projectile lying on the map.
struct SessionObject {
    std::uint16_t descriptor_index = 0;
    std::string name;  // the ITEMS.TXT name of what it is, when it is an item
    int item_id = 0;   // indexes ITEMS.TXT; 0 when the object is not an item
    render::Vec3 position;
};

enum class MapSessionError : std::uint8_t {
    None,
    NoArchive,   // Games.lod is missing or unreadable
    NotFound,    // the archive holds no map of that name
    BadMap,      // the map entry did not parse
    UnknownKind  // the name ends in neither .odm nor .blv
};

// A loaded map and everything placed on it.
//
// Outdoor and indoor maps are stored in different formats and were read by two
// separate programs for most of this project's life. What they have in common
// is most of what a renderer wants: a collision world, decorations, monsters,
// loot, a spawn point, and the tables that turn names into pictures. This
// holds that common shape, plus whichever geometry the map actually has.
struct MapSession {
    MapKind kind = MapKind::Unknown;

    std::string file_name;     // the Games.lod entry, e.g. "OutA1.Odm"
    std::string display_name;  // from MapStats.txt; empty when unlisted
    int music_track = 0;       // the N in Sounds/N.mp3; 0 when unlisted
    int refill_days = 0;       // how long this map takes to refill with monsters
    int treasure_level = 0;    // MapStats' "Tres 0-6"; what its chests hold
    std::vector<std::uint16_t> chest_looks;  // each chest's DCHEST row
    std::vector<SessionNpc> everyone;  // the whole NPC roster, for arrivals
    // What ground each tile index is, in the footstep sounds' own words
    // ("Grass", "Snow", "Water"...); empty for indoor maps.
    std::array<std::string, 256> tile_grounds{};

    // The ground under a point, in the footstep sounds' vocabulary:
    // outdoor by the tile's art, indoor always the stone hall. `inferred`
    [[nodiscard]] std::string_view ground_at(float x, float z) const;
    int trap_difficulty = 0;   // MapStats' "Trap 0-10"; how its chests bite
    int lock_difficulty = 0;   // MapStats' "Lock 0-10"; how they resist opening

    // Outdoor geometry.
    OdmMap odm;
    OdmTerrain terrain;
    render::TerrainMesh terrain_mesh;
    render::TileSet tiles;
    std::vector<OdmModelMesh> meshes;
    std::vector<OdmModel> models;
    OdmTileIndex tile_index;                    // what stands near each terrain tile
    std::vector<OdmSpawnPoint> monster_spawns;  // where the map puts monsters

    // What a refill needs to roll the spawn points again: the map's design
    // row id, its three encounter slots, and how many of the actors were
    // placed by the event file rather than rolled — those stay.
    int map_id = 0;
    std::array<data::MapEncounter, 3> encounters;
    std::size_t placed_actor_count = 0;

    // Indoor geometry.
    BlvMap blv;

    // The doors the event file places on an indoor map, with a live open
    // flag per door. Bases are the shut position on 4,067 of 4,067 vertices
    // across the 52 maps.
    std::vector<MapDoor> doors;

    // Shared.
    CollisionWorld collision;
    std::vector<SessionDecoration> decorations;
    std::vector<SessionActor> actors;
    std::vector<SessionObject> objects;
    std::vector<SessionBuilding> buildings;
    render::Vec3 spawn{};

    // The global tables the placed things resolve through.
    SpriteFrameTable sprite_frames;

    // The map's own event script and the strings it prints. Both live in
    // icons.lod beside the design tables; see docs/formats/map-events.md.
    MapScript script;
    MapStrings script_strings;
    SoundTable sounds;
    ObjectTable object_descriptors;
    DecorationTable decoration_types;
    MonsterList monsters;

    [[nodiscard]] bool outdoor() const noexcept { return kind == MapKind::Outdoor; }
    [[nodiscard]] bool indoor() const noexcept { return kind == MapKind::Indoor; }

    // The name to show a player: the design table's, falling back to the file.
    [[nodiscard]] const std::string& title() const noexcept {
        return display_name.empty() ? file_name : display_name;
    }

    // Height of the outdoor terrain under a point, interpolated across the
    // cell. Returns 0 for an indoor map, which has no heightfield.
    [[nodiscard]] float terrain_height_at(float x, float z) const;

    // The decorations the map lists against the tile under a render-space
    // point, as indices into `decorations`. This is the map's own answer, not
    // a search: outdoor maps ship the list. Indoor maps have no such index and
    // get an empty answer, so callers keep whatever they did before.
    [[nodiscard]] std::vector<std::size_t> decorations_near(float x, float z) const;

    // The same answer without allocating, for callers asking once per monster
    // per frame. `out` is cleared first.
    void decorations_near(float x, float z, std::vector<std::size_t>& out) const;
};

// Load a map by its Games.lod entry name, e.g. "OutA1.Odm" or "D01.blv".
// The kind is chosen by the name's extension.
//
// `cache` is used to decide which monster art this installation actually has,
// and is left holding whatever textures the load resolved.
[[nodiscard]] MapSessionError load_map_session(const std::filesystem::path& games_lod,
                                               const std::filesystem::path& data_dir,
                                               std::string_view map_name, assets::AssetCache& cache,
                                               MapSession& out);

// Roll the spawn points' monsters again, leaving the placed actors alone.
// This is what a refill means on a map that ships spawn points: new groups at
// the same points, sized by the same encounter slots. A different seed gives
// a different population.
void respawn_monsters(const data::MonsterStatsTable& monsters, assets::AssetCache& cache,
                      std::uint32_t seed, MapSession& out);

// Stand one more monster on the map, which is what the summon opcode does.
// Outdoor positions settle onto the terrain. Returns false when the id is
// out of the table or this install has no art for it.
bool summon_actor(const data::MonsterStatsTable& monsters, assets::AssetCache& cache,
                  int monster_id, render::Vec3 position, MapSession& out);

// Rebuild the indoor collision world from the faces as they now stand.
// Moving a door's vertices leaves the collision polygons where they were;
// this is how the world pushes back at the new geometry.
void rebuild_indoor_collision(MapSession& out);

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up.
[[nodiscard]] inline render::Vec3 to_render_space(int x, int y, int z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_SESSION_HPP
