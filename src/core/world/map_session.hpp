#ifndef STARHAVEN_CORE_WORLD_MAP_SESSION_HPP
#define STARHAVEN_CORE_WORLD_MAP_SESSION_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/assets/asset_cache.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/render/math3d.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/tile_set.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/collision.hpp"
#include "core/world/decoration_table.hpp"
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
struct SessionBuilding {
    std::string name;
    std::string type;
    std::string proprietor;
    int opens = 0;
    int closes = 0;

    // The people the NPC table places inside, as "Name, Profession", and the
    // `npcprof.txt` id of each — which is what `PROFTEXT.txt` is keyed by, so
    // an occupant can be asked what their trade says today.
    std::vector<std::string> occupants;
    std::vector<int> occupant_professions;
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

    // Outdoor geometry.
    OdmMap odm;
    OdmTerrain terrain;
    render::TerrainMesh terrain_mesh;
    render::TileSet tiles;
    std::vector<OdmModelMesh> meshes;
    std::vector<OdmModel> models;
    OdmTileIndex tile_index;                    // what stands near each terrain tile
    std::vector<OdmSpawnPoint> monster_spawns;  // where the map puts monsters

    // Indoor geometry.
    BlvMap blv;

    // Shared.
    CollisionWorld collision;
    std::vector<SessionDecoration> decorations;
    std::vector<SessionActor> actors;
    std::vector<SessionObject> objects;
    std::vector<SessionBuilding> buildings;
    render::Vec3 spawn{};

    // The global tables the placed things resolve through.
    SpriteFrameTable sprite_frames;
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

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up.
[[nodiscard]] inline render::Vec3 to_render_space(int x, int y, int z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_MAP_SESSION_HPP
