#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/image/bitmap.hpp"
#include "core/image/palette.hpp"
#include "core/image/sprite.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/math3d.hpp"
#include "core/render/rasterizer.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/texture.hpp"
#include "core/render/tile_set.hpp"
#include "core/world/collision.hpp"
#include "core/world/map_event.hpp"
#include "core/world/monster_list.hpp"
#include "core/world/odm_map.hpp"
#include "core/world/tile_table.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.odm>\n"
              << "\n"
              << "Loads one .odm outdoor map from your own legal game install\n"
              << "(Games.lod) and renders its terrain as a 3D heightfield you\n"
              << "can walk around. Software-rasterized (no OpenGL).\n"
              << "\n"
              << "Controls:\n"
              << "  W/A/S/D    move forward/left/back/right\n"
              << "  Q/E        descend/ascend (fly)\n"
              << "  Shift      move faster\n"
              << "  Arrows     look left/right/up/down\n"
              << "  ESC/close  quit\n"
              << "\n  --screenshot FILE   render one frame to a PPM and exit\n"
              << "  --pos X,Y,Z         start position (renderer axes, Y up)\n"
              << "  --look YAW,PITCH    start orientation in degrees\n"
              << "  --boxes             overlay model bounding boxes\n"
              << "  --fly               disable gravity and collision\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory.\n";
}

std::filesystem::path resolve_games_lod() {
    namespace fs = std::filesystem;
    if (auto install = starhaven::platform::install_from_env()) {
        fs::path p = *install / "data" / "Games.lod";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "data/Games.lod";
}

// Player proportions, in MM6 world units. A terrain cell is 512 across.
constexpr float kBodyRadius = 64.0f;
constexpr float kBodyHeight = 320.0f;
constexpr float kEyeHeight = 280.0f;
constexpr float kStepHeight = 96.0f;
constexpr float kGravity = -2400.0f;
constexpr float kMouseSensitivity = 0.0025f;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Populate `out` with the ground textures this map's tilemap actually
// references. Returns the number of distinct tiles resolved, or -1 if the
// archives could not be opened.
//
// Only the indices present in the tilemap are decoded: the global table has
// 882 records, of which a single map typically uses fewer than a hundred.
int load_ground_tiles(const starhaven::world::OdmTerrain& terrain,
                      starhaven::render::TileSet& out) {
    namespace lod = starhaven::lod;
    namespace img = starhaven::image;
    namespace world = starhaven::world;

    const auto install = starhaven::platform::install_from_env();
    if (!install) return -1;
    const std::filesystem::path data = *install / "data";

    lod::LodArchive icons;
    lod::LodArchive bitmaps;
    if (lod::LodArchive::open(data / "icons.lod", icons) != lod::LodError::None ||
        lod::LodArchive::open(data / "BITMAPS.LOD", bitmaps) != lod::LodError::None) {
        return -1;
    }

    std::span<const std::byte> dtile;
    if (icons.payload("DTILE.BIN", dtile) != lod::LodArchive::PayloadError::None) {
        return -1;
    }
    world::TileTable table;
    if (world::TileTable::parse(dtile, table) != world::TileTableError::None) {
        return -1;
    }

    // Distinct indices only; the tilemap is 16384 cells over ~90 tiles.
    std::array<bool, 256> used{};
    for (std::uint8_t t : terrain.tilemap) used[t] = true;

    int resolved = 0;
    for (int i = 0; i < 256; ++i) {
        if (!used[static_cast<std::size_t>(i)]) continue;
        const auto* rec = table.at(static_cast<std::uint8_t>(i));
        // An empty name is a reserved slot, not an error: the shipped table
        // has more rows than art. Leaving the slot empty makes the rasterizer
        // skip those triangles rather than draw garbage.
        if (rec == nullptr || rec->name.empty()) continue;

        std::span<const std::byte> raw;
        if (bitmaps.payload(rec->name, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        img::Bitmap bmp;
        if (img::decode_bitmap(raw, bmp) != img::BitmapError::None) continue;

        starhaven::render::Texture tex;
        if (!starhaven::render::Texture::create(bmp.width, bmp.height,
                                              std::move(bmp.rgba), tex)) {
            continue;
        }
        if (out.set(static_cast<std::uint8_t>(i), std::move(tex))) ++resolved;
    }
    return resolved;
}

// Decode the textures the map's model facets reference, keyed by the facet's
// texture name. Returns the number of distinct textures resolved, or -1 if
// BITMAPS.LOD could not be opened.
int load_model_textures(const std::vector<starhaven::world::OdmModelMesh>& meshes,
                        std::map<std::string, starhaven::render::Texture>& out) {
    namespace lod = starhaven::lod;
    namespace img = starhaven::image;

    const auto install = starhaven::platform::install_from_env();
    if (!install) return -1;

    lod::LodArchive bitmaps;
    if (lod::LodArchive::open(*install / "data" / "BITMAPS.LOD", bitmaps) !=
        lod::LodError::None) {
        return -1;
    }

    int resolved = 0;
    for (const auto& mesh : meshes) {
        for (const auto& f : mesh.facets) {
            if (f.texture_name.empty() || out.count(f.texture_name) != 0) continue;

            std::span<const std::byte> raw;
            if (bitmaps.payload(f.texture_name, raw) !=
                lod::LodArchive::PayloadError::None) {
                continue;
            }
            img::Bitmap bmp;
            if (img::decode_bitmap(raw, bmp) != img::BitmapError::None) continue;
            starhaven::render::Texture tex;
            if (!starhaven::render::Texture::create(bmp.width, bmp.height,
                                                  std::move(bmp.rgba), tex)) {
                continue;
            }
            out.emplace(f.texture_name, std::move(tex));
            ++resolved;
        }
    }
    return resolved;
}

// Decode a set of named sprites into `out`. Sprites live in SPRITES.LOD and
// share palettes held in BITMAPS.LOD.
int load_sprites(const std::vector<std::string>& names,
                 std::map<std::string, starhaven::render::Texture>& out) {
    namespace lod = starhaven::lod;
    namespace img = starhaven::image;

    const auto install = starhaven::platform::install_from_env();
    if (!install) return -1;
    const std::filesystem::path data = *install / "data";

    lod::LodArchive sprites;
    lod::LodArchive bitmaps;
    if (lod::LodArchive::open(data / "SPRITES.LOD", sprites) != lod::LodError::None ||
        lod::LodArchive::open(data / "BITMAPS.LOD", bitmaps) != lod::LodError::None) {
        return -1;
    }

    // Palettes are shared between sprites, so decode each one once.
    std::map<std::uint16_t, img::Palette> palettes;
    auto palette_for = [&](std::uint16_t id, img::Palette& into) {
        if (const auto it = palettes.find(id); it != palettes.end()) {
            into = it->second;
            return true;
        }
        std::span<const std::byte> bytes;
        if (bitmaps.payload(img::palette_entry_name(id), bytes) !=
            lod::LodArchive::PayloadError::None) {
            return false;
        }
        img::Palette pal;
        // Palette entries are a 48-byte zero-image header then 768 RGB bytes.
        if (img::extract_palette(bytes, /*data_offset=*/48, pal) !=
            img::PaletteError::None) {
            return false;
        }
        palettes.emplace(id, pal);
        into = pal;
        return true;
    };

    int resolved = 0;
    for (const auto& name : names) {
        if (name.empty() || out.count(name) != 0) continue;

        std::span<const std::byte> raw;
        if (sprites.payload(name, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        img::SpriteHeader header;
        if (img::read_sprite_header(raw, header) != img::SpriteError::None) continue;
        img::Palette palette;
        if (!palette_for(header.palette_id, palette)) continue;

        img::Sprite sprite;
        if (img::decode_sprite(raw, palette, sprite) != img::SpriteError::None) {
            continue;
        }
        starhaven::render::Texture tex;
        if (!starhaven::render::Texture::create(sprite.width, sprite.height,
                                                std::move(sprite.rgba), tex)) {
            continue;
        }
        out.emplace(name, std::move(tex));
        ++resolved;
    }
    return resolved;
}

// Whether SPRITES.LOD holds an entry of this name. Opened once and kept, so
// probing many candidate names stays cheap.
bool sprite_exists(const std::string& name) {
    static starhaven::lod::LodArchive archive;
    static bool opened = [] {
        const auto install = starhaven::platform::install_from_env();
        return install && starhaven::lod::LodArchive::open(
                              *install / "data" / "SPRITES.LOD", archive) ==
                              starhaven::lod::LodError::None;
    }();
    if (!opened) return false;
    std::span<const std::byte> raw;
    return archive.payload(name, raw) ==
           starhaven::lod::LodArchive::PayloadError::None;
}

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up. Model
// geometry is stored in absolute world units on the same scale as the terrain
// (verified in docs/formats/odm-model-facets.md), so placement is a pure axis
// swap with no offset.
starhaven::render::Vec3 to_render_space(std::int32_t x, std::int32_t y, std::int32_t z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

// Height of the terrain under a renderer-space point, interpolated across the
// cell so walking a slope is smooth rather than stepped.
float terrain_height_at(const starhaven::world::OdmTerrain& terrain,
                        starhaven::render::TerrainScale scale, float x, float z) {
    constexpr int dim = starhaven::world::OdmTerrain::kGridDim;
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
        const std::size_t idx = static_cast<std::size_t>(cz) * dim + cx;
        return static_cast<float>(terrain.heightmap[idx]) * scale.height_scale;
    };
    const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * fx;
    const float bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * fx;
    return top + (bottom - top) * fz;
}

// Transform one world-space vertex through view+projection, then to a screen
// vertex. Returns false if the vertex is behind the near plane (caller should
// have clipped already).
bool project(const starhaven::render::Mat4& view_proj,
             starhaven::render::Vec3 world, float r, float g, float b,
             starhaven::render::Vec2 uv, starhaven::render::ScreenVertex& out) {
    using namespace starhaven::render;
    const Vec4 clip = view_proj * Vec4{world.x, world.y, world.z, 1.0f};
    if (clip.w <= 0.0001f) {
        return false;  // behind/through the camera
    }
    const float inv_w = 1.0f / clip.w;
    out.x = (clip.x * inv_w * 0.5f + 0.5f) * kWidth;
    out.y = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * kHeight;  // flip Y
    out.z = clip.z * inv_w;
    out.r = r;
    out.g = g;
    out.b = b;
    out.u = uv.u;
    out.v = uv.v;
    // Keep 1/w: the rasterizer needs it for perspective-correct texturing,
    // and this is the only place it is known.
    out.inv_w = inv_w;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_name;
    std::string screenshot;  // when set, render one frame to PPM and exit
    bool show_boxes = false;  // model bounding-box wireframe overlay
    bool fly = false;         // free-look camera, no gravity or collision
    // Camera defaults: above the map center, looking across it.
    starhaven::render::Vec3 start_pos{0, 32.0f * 30.0f, 0};
    float start_yaw = 0.6f, start_pitch = -0.3f;  // radians

    // Parse "a,b,c" into up to three floats; returns how many were read.
    auto parse_floats = [](const std::string& s, float* out, int max_count) {
        int n = 0;
        std::size_t pos = 0;
        while (n < max_count && pos <= s.size()) {
            const std::size_t comma = s.find(',', pos);
            const std::string field = s.substr(pos, comma - pos);
            if (field.empty()) break;
            out[n++] = std::strtof(field.c_str(), nullptr);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return n;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (a == "--boxes") {
            show_boxes = true;
        } else if (a == "--fly") {
            fly = true;
        } else if (a == "--pos" && i + 1 < argc) {
            float xyz[3] = {0, 0, 0};
            if (parse_floats(argv[++i], xyz, 3) != 3) {
                print_usage(argv[0]);
                return 2;
            }
            start_pos = {xyz[0], xyz[1], xyz[2]};
        } else if (a == "--look" && i + 1 < argc) {
            float yp[2] = {0, 0};
            if (parse_floats(argv[++i], yp, 2) != 2) {
                print_usage(argv[0]);
                return 2;
            }
            start_yaw = starhaven::render::radians(yp[0]);
            start_pitch = starhaven::render::radians(yp[1]);
        } else if (map_name.empty()) {
            map_name = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (map_name.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    namespace lod = starhaven::lod;
    namespace world = starhaven::world;
    namespace render = starhaven::render;

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(resolve_games_lod(), archive) != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (archive.payload(map_name, entry) != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: map not found: " << map_name << "\n";
        return 1;
    }
    world::OdmMap map;
    world::OdmTerrain terrain;
    if (world::parse_odm_terrain(entry, map, terrain) != world::OdmError::None) {
        std::cerr << "error: could not parse ODM\n";
        return 1;
    }

    const render::TerrainMesh mesh = render::build_terrain_mesh(terrain, {});

    // Real ground textures, resolved through the chain documented in
    // docs/formats/dtile.md: tilemap byte -> DTILE.BIN record -> record.name
    // -> BITMAPS.LOD entry. Falls back to generated placeholders if the
    // archives are missing, so the viewer still runs on a partial install.
    render::TileSet tiles;
    const int loaded = load_ground_tiles(terrain, tiles);
    if (loaded <= 0) {
        std::cerr << "note: no ground tiles resolved; using placeholders\n";
        tiles = render::TileSet::make_placeholder();
    } else {
        std::cout << "loaded " << loaded << " ground tile textures\n";
    }

    std::vector<world::OdmModel> models;
    if (world::extract_models(map, models) != world::OdmError::None) {
        models.clear();
    }
    // Every model's mesh: vertices plus the facets (polygons) over them.
    std::vector<world::OdmModelMesh> meshes;
    if (world::extract_model_meshes(map, meshes) != world::OdmError::None) {
        std::cerr << "note: model geometry did not decode; drawing bounds only\n";
        meshes.clear();
    }
    std::vector<world::OdmDecoration> decorations;
    if (world::extract_decorations(map, decorations) != world::OdmError::None) {
        decorations.clear();
    }
    std::map<std::string, render::Texture> decoration_sprites;
    std::vector<std::string> deco_names;
    deco_names.reserve(decorations.size());
    for (const auto& d : decorations) deco_names.push_back(d.name);
    const int deco_loaded = load_sprites(deco_names, decoration_sprites);
    if (!decorations.empty()) {
        std::cout << "loaded " << decorations.size() << " decorations, "
                  << (deco_loaded > 0 ? deco_loaded : 0) << " sprites\n";
    }

    // Collision: the models' own facets. Terrain is handled separately, by
    // sampling the heightfield, which is both cheaper and exactly right.
    world::CollisionWorld collision;
    {
        std::vector<render::Vec3> corners;
        for (const auto& mesh : meshes) {
            for (const auto& f : mesh.facets) {
                if (f.vertex_count < 3) continue;
                corners.clear();
                for (std::size_t k = 0; k < f.vertex_count; ++k) {
                    const auto& v = mesh.vertices[f.vertex_ids[k]];
                    corners.push_back(to_render_space(v.x, v.y, v.z));
                }
                collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
            }
        }
    }
    std::cout << "collision polygons: " << collision.size() << "\n";

    // Actors: the map's event file names the monsters standing on it, and the
    // monster table turns each id into sprite base names.
    std::vector<world::MapActor> actors;
    std::map<std::string, render::Texture> actor_sprites;
    std::map<std::size_t, std::string> actor_sprite_name;
    {
        // The event file shares the map's stem with a .ddm extension.
        std::string stem = map_name;
        const std::size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        std::span<const std::byte> ev_entry;
        world::MapEventFile ev;
        if (archive.payload(stem + ".ddm", ev_entry) ==
                lod::GameLodArchive::PayloadError::None &&
            world::parse_map_event(ev_entry, ev) == world::MapEventError::None) {
            actors = world::extract_actors(ev);
        }

        world::MonsterList monsters;
        if (const auto install = starhaven::platform::install_from_env()) {
            lod::LodArchive icons;
            std::span<const std::byte> raw;
            if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) ==
                    lod::LodError::None &&
                icons.payload("DMONLIST.BIN", raw) ==
                    lod::LodArchive::PayloadError::None) {
                if (world::MonsterList::parse(raw, monsters) !=
                    world::MonsterListError::None) {
                    monsters = world::MonsterList{};
                }
            }
        }

        // Draw the standing pose, front view. MM6 sprites carry a view digit
        // 0..4; picking the angle-correct one is a later refinement.
        //
        // Monsters come in A/B/C triples and only the A variant's sprite ships
        // in SPRITES.LOD — B and C are presumably palette swaps, a mechanism
        // that is not decoded. Falling back to the group's A sprite draws them
        // in the wrong colours rather than not at all.
        std::vector<std::string> names;
        for (std::size_t i = 0; i < actors.size(); ++i) {
            const std::size_t id = actors[i].monster_id;
            const auto* m = monsters.at(id);
            if (m == nullptr) continue;

            auto sprite_for = [&](const world::MonsterListEntry* e) {
                const std::string& base = e->animation(world::MonsterAnimation::Stand);
                return base.empty() ? std::string{} : base + "0";
            };
            std::string sprite = sprite_for(m);
            if (!sprite.empty() && !sprite_exists(sprite)) {
                const auto* a_variant = monsters.at(id - (id % 3));
                if (a_variant != nullptr) {
                    const std::string alt = sprite_for(a_variant);
                    if (!alt.empty() && sprite_exists(alt)) sprite = alt;
                }
            }
            if (sprite.empty() || !sprite_exists(sprite)) continue;
            actor_sprite_name[i] = sprite;
            names.push_back(sprite);
        }
        const int actor_loaded = load_sprites(names, actor_sprites);
        if (!actors.empty()) {
            std::cout << "loaded " << actors.size() << " actors, "
                      << (actor_loaded > 0 ? actor_loaded : 0) << " sprites\n";
        }
    }

    std::map<std::string, render::Texture> model_textures;
    const int model_tex = load_model_textures(meshes, model_textures);
    if (!meshes.empty()) {
        std::size_t facets = 0;
        for (const auto& mesh : meshes) facets += mesh.facets.size();
        std::cout << "loaded " << meshes.size() << " model meshes, " << facets
                  << " facets, " << (model_tex > 0 ? model_tex : 0)
                  << " facet textures\n";
    }

    // SDL3 returns true on success, unlike SDL2's 0-on-success convention.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "StarHaven — walk — " + map_name;
    // SDL3 drops the x/y arguments and SDL_WINDOW_SHOWN; nullptr picks the
    // default render backend.
    SDL_Window* window = SDL_CreateWindow(title.c_str(), kWidth, kHeight, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        kWidth, kHeight);

    // Camera state (overridable with --pos / --look).
    render::Vec3 cam_pos = start_pos;
    float yaw = start_yaw;      // radians
    float pitch = start_pitch;
    float fall_speed = 0.0f;

    const bool mouse_look = screenshot.empty();
    if (mouse_look) {
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    const render::Vec3 sun = render::normalize(render::Vec3{0.4f, 1.0f, 0.3f});
    render::Framebuffer fb(kWidth, kHeight);

    // A capture taken on the first frame shows the camera before gravity has
    // settled it, which misrepresents where the player actually stands.
    constexpr int kSettleFrames = 90;
    int frame = 0;

    bool running = true;
    while (running) {
        ++frame;
        // --- input ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_look) {
                yaw += event.motion.xrel * kMouseSensitivity;
                pitch -= event.motion.yrel * kMouseSensitivity;
            }
        }
        // SDL3 hands back `const bool*` here; SDL2 used `const Uint8*`.
        const auto* keys = SDL_GetKeyboardState(nullptr);
        const float speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;
        const float dt = 1.0f / 60.0f;

        const render::Vec3 fwd = render::camera_forward(yaw, pitch);
        const render::Vec3 fwd_flat = render::normalize(render::Vec3{fwd.x, 0, fwd.z});
        const render::Vec3 right = render::camera_right(yaw);

        render::Vec3 wish = cam_pos;
        if (keys[SDL_SCANCODE_W]) wish = wish + fwd_flat * (speed * dt);
        if (keys[SDL_SCANCODE_S]) wish = wish - fwd_flat * (speed * dt);
        if (keys[SDL_SCANCODE_A]) wish = wish - right * (speed * dt);
        if (keys[SDL_SCANCODE_D]) wish = wish + right * (speed * dt);

        if (fly) {
            if (keys[SDL_SCANCODE_Q]) wish.y -= speed * dt;
            if (keys[SDL_SCANCODE_E]) wish.y += speed * dt;
            cam_pos = wish;
        } else {
            const render::Vec3 feet_from{cam_pos.x, cam_pos.y - kEyeHeight, cam_pos.z};
            const render::Vec3 feet_to{wish.x, wish.y - kEyeHeight, wish.z};
            render::Vec3 feet =
                collision.slide(feet_from, feet_to, kBodyRadius, kBodyHeight);

            fall_speed += kGravity * dt;
            feet.y += fall_speed * dt;

            // The ground is whichever is higher: the terrain, or a model
            // surface (a bridge deck, a floor inside a building).
            float ground = terrain_height_at(terrain, {}, feet.x, feet.z);
            float model_floor = 0.0f;
            if (collision.floor_below({feet.x, feet.y + kStepHeight, feet.z},
                                      model_floor)) {
                ground = std::max(ground, model_floor);
            }
            if (feet.y <= ground) {
                feet.y = ground;
                fall_speed = 0.0f;
            }
            cam_pos = {feet.x, feet.y + kEyeHeight, feet.z};
        }

        if (keys[SDL_SCANCODE_LEFT]) yaw -= 1.5f * dt;
        if (keys[SDL_SCANCODE_RIGHT]) yaw += 1.5f * dt;
        if (keys[SDL_SCANCODE_UP]) pitch += 1.5f * dt;
        if (keys[SDL_SCANCODE_DOWN]) pitch -= 1.5f * dt;
        pitch = std::clamp(pitch, -1.4f, 1.4f);

        // --- render ---
        fb.clear({135, 180, 220, 255});   // sky
        fb.clear_depth(1.0f);

        const render::Vec3 target = cam_pos + fwd;
        const render::Mat4 view = render::mat4_look_at(cam_pos, target, {0, 1, 0});
        const float aspect = static_cast<float>(kWidth) / kHeight;
        const render::Mat4 proj = render::mat4_perspective(render::radians(60.0f),
                                                           aspect, 1.0f, 20000.0f);
        const render::Mat4 view_proj = proj * view;

        // Rasterize one flat-shaded world-space triangle: near-clip in view
        // space, project, then hand each resulting triangle to the rasterizer.
        // Terrain and model facets share this path; they differ only in which
        // texture they sample and whether backfaces are culled.
        auto draw_world_triangle = [&](const render::Vec3 w[3],
                                       const render::Vec2 uv[3], float shade,
                                       const render::Texture& texture,
                                       render::WrapMode wrap, bool cull) {
            render::ViewVertex vv[3];
            for (int k = 0; k < 3; ++k) {
                const render::Vec4 vp = view * render::Vec4{w[k].x, w[k].y, w[k].z, 1};
                vv[k] = {vp.x, vp.y, vp.z, shade, shade, shade, uv[k].u, uv[k].v};
            }
            std::vector<render::ViewVertex> clipped;
            render::clip_near(vv, /*near_z*/ -1.0f, clipped);

            for (std::size_t t = 0; t + 2 < clipped.size(); t += 3) {
                render::ScreenVertex s[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    // `clipped` is already in view space, so only the
                    // projection matrix may be applied here. Using view_proj
                    // would transform by the view matrix a second time.
                    if (!project(proj,
                                 {clipped[t + k].x, clipped[t + k].y, clipped[t + k].z},
                                 clipped[t + k].r, clipped[t + k].g, clipped[t + k].b,
                                 {clipped[t + k].u, clipped[t + k].v}, s[k])) {
                        ok = false; break;
                    }
                }
                if (!ok) continue;
                // Textured drawing is a no-op without art, so fall back to a
                // flat fill: an untextured surface still reads as solid.
                if (texture.empty()) {
                    fb.draw_triangle(s[0], s[1], s[2], cull);
                } else {
                    fb.draw_triangle_textured(s[0], s[1], s[2], texture, wrap, cull);
                }
            }
        };

        // Rasterize triangles with near-plane clipping + flat shading.
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const render::Vec3 w0 = mesh.vertices[mesh.indices[i]];
            const render::Vec3 w1 = mesh.vertices[mesh.indices[i + 1]];
            const render::Vec3 w2 = mesh.vertices[mesh.indices[i + 2]];
            const render::Vec2 t0 = mesh.uvs[mesh.indices[i]];
            const render::Vec2 t1 = mesh.uvs[mesh.indices[i + 1]];
            const render::Vec2 t2 = mesh.uvs[mesh.indices[i + 2]];
            const std::uint8_t tile_id = mesh.tile_ids[i / 3];

            // Average per-vertex normal -> flat face normal for shading.
            const render::Vec3 n = render::normalize(
                (mesh.normals[mesh.indices[i]] +
                 mesh.normals[mesh.indices[i + 1]] +
                 mesh.normals[mesh.indices[i + 2]]) * (1.0f / 3.0f));
            float lambert = render::dot(n, sun);
            lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.8f + 0.2f;  // ambient

            // Flat shading: the scalar Lambert term drives all three channels,
            // so terrain stays grayscale-modulated as before.
            const render::Vec3 w[3] = {w0, w1, w2};
            const render::Vec2 uv[3] = {t0, t1, t2};
            // UVs are in cell units, so Repeat lays one tile per cell.
            draw_world_triangle(w, uv, lambert, tiles.texture_for(tile_id),
                                render::WrapMode::Repeat,
                                /*cull_backfaces*/ true);
        }

        // Model meshes: every facet is a convex n-gon over its model's own
        // vertex array, triangulated here as a fan.
        //
        // Backfaces are not culled. The MM6->renderer axis swap mirrors the
        // space, so on-disk winding no longer predicts screen winding, and the
        // attribute bit that marks a facet two-sided is not yet identified.
        // The z-buffer resolves the overdraw correctly either way.
        for (const auto& model_mesh : meshes) {
            for (const auto& f : model_mesh.facets) {
                if (f.vertex_count < 3) continue;  // degenerate; nothing to fill

                // The facet's own plane normal, in renderer axes.
                const render::Vec3 n =
                    render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
                // Facets face both ways here, so light the side being seen.
                float lambert = std::abs(render::dot(n, sun));
                lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.8f + 0.2f;

                const auto tex_it = model_textures.find(f.texture_name);
                const bool has_tex = tex_it != model_textures.end();
                // Texture coordinates are stored in texels; the rasterizer
                // wants them normalized to the texture it samples.
                const float inv_w = has_tex && tex_it->second.width() > 0
                    ? 1.0f / static_cast<float>(tex_it->second.width()) : 0.0f;
                const float inv_h = has_tex && tex_it->second.height() > 0
                    ? 1.0f / static_cast<float>(tex_it->second.height()) : 0.0f;

                auto corner = [&](std::size_t k) {
                    const auto& v = model_mesh.vertices[f.vertex_ids[k]];
                    return to_render_space(v.x, v.y, v.z);
                };
                auto corner_uv = [&](std::size_t k) {
                    return render::Vec2{static_cast<float>(f.u[k]) * inv_w,
                                        static_cast<float>(f.v[k]) * inv_h};
                };

                for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
                    const render::Vec3 w[3] = {corner(0), corner(k), corner(k + 1)};
                    const render::Vec2 uv[3] = {corner_uv(0), corner_uv(k),
                                                corner_uv(k + 1)};
                    if (has_tex) {
                        draw_world_triangle(w, uv, lambert, tex_it->second,
                                            render::WrapMode::Repeat,
                                            /*cull_backfaces*/ false);
                    } else {
                        // No art for this facet: fill it flat rather than
                        // dropping it, so the prop still reads as solid.
                        draw_world_triangle(w, uv, lambert * 0.7f,
                                            render::Texture{},
                                            render::WrapMode::Repeat, false);
                    }
                }
            }
        }

        // Decorations: camera-facing billboards standing on the ground. Drawn
        // after the solid geometry so the z-buffer resolves them against it;
        // the rasterizer skips fully transparent texels, which is what gives
        // the sprites their cut-out silhouette.
        {
            const render::Vec3 bb_right = render::camera_right(yaw);
            for (const auto& d : decorations) {
                const auto it = decoration_sprites.find(d.name);
                if (it == decoration_sprites.end() || it->second.empty()) continue;

                // Sprite pixels are not world units and the decoration table
                // states no size, so the factor is calibrated by eye against
                // the models: at 1.0 a tree stands shorter than a cottage
                // door, which is plainly wrong. `inferred`
                constexpr float kSpriteScale = 4.0f;
                const float w = static_cast<float>(it->second.width()) * kSpriteScale;
                const float h = static_cast<float>(it->second.height()) * kSpriteScale;
                const render::Vec3 base = to_render_space(d.x, d.y, d.z);
                const render::Vec3 half = bb_right * (w * 0.5f);

                // Corners: bottom-left, bottom-right, top-right, top-left.
                const render::Vec3 bl = base - half;
                const render::Vec3 br = base + half;
                const render::Vec3 tr{br.x, br.y + h, br.z};
                const render::Vec3 tl{bl.x, bl.y + h, bl.z};

                const render::Vec3 t0[3] = {tl, bl, br};
                const render::Vec2 u0[3] = {{0, 0}, {0, 1}, {1, 1}};
                draw_world_triangle(t0, u0, 1.0f, it->second,
                                    render::WrapMode::Clamp, false);
                const render::Vec3 t1[3] = {tl, br, tr};
                const render::Vec2 u1[3] = {{0, 0}, {1, 1}, {1, 0}};
                draw_world_triangle(t1, u1, 1.0f, it->second,
                                    render::WrapMode::Clamp, false);
            }
        }

        // Actors, drawn the same way as decorations.
        {
            const render::Vec3 bb_right = render::camera_right(yaw);
            for (std::size_t i = 0; i < actors.size(); ++i) {
                const auto named = actor_sprite_name.find(i);
                if (named == actor_sprite_name.end()) continue;
                const auto it = actor_sprites.find(named->second);
                if (it == actor_sprites.end() || it->second.empty()) continue;

                // Actor sprites are drawn near 1:1: a standing peasant is 255
                // pixels tall and should stand about 300 world units, which is
                // roughly the party's own eye height. Decoration sprites need a
                // much larger factor, so the two are not shared. `inferred`
                constexpr float kActorScale = 1.2f;
                const float w = static_cast<float>(it->second.width()) * kActorScale;
                const float h = static_cast<float>(it->second.height()) * kActorScale;
                const render::Vec3 base =
                    to_render_space(actors[i].x, actors[i].y, actors[i].z);
                const render::Vec3 half = bb_right * (w * 0.5f);
                const render::Vec3 bl = base - half;
                const render::Vec3 br = base + half;
                const render::Vec3 tr{br.x, br.y + h, br.z};
                const render::Vec3 tl{bl.x, bl.y + h, bl.z};

                const render::Vec3 t0[3] = {tl, bl, br};
                const render::Vec2 u0[3] = {{0, 0}, {0, 1}, {1, 1}};
                draw_world_triangle(t0, u0, 1.0f, it->second,
                                    render::WrapMode::Clamp, false);
                const render::Vec3 t1[3] = {tl, br, tr};
                const render::Vec2 u1[3] = {{0, 0}, {1, 1}, {1, 0}};
                draw_world_triangle(t1, u1, 1.0f, it->second,
                                    render::WrapMode::Clamp, false);
            }
        }

        // Optional wireframe overlay of the model bounding boxes (--boxes).
        // Model coordinates are in MM6 world units, matching the terrain scale,
        // and go through the same axis swap as the meshes.
        const render::Color box_color{255, 220, 0, 255};
        // Debug overlays are drawn with draw_line, which ignores UVs.
        auto project_box_vert = [&](render::Vec3 world, render::ScreenVertex& out) {
            return project(view_proj, world, 1.0f, 1.0f, 1.0f, {0.0f, 0.0f}, out);
        };
        for (std::size_t mi = 0; show_boxes && mi < models.size(); ++mi) {
            const world::OdmModel& m = models[mi];
            render::ScreenVertex c[8];
            // Corners of the box in MM6 axes: the four (x,y) corners at min_z,
            // then the same four at max_z (z being MM6's up axis).
            const bool ok =
                project_box_vert(to_render_space(m.min_x, m.min_y, m.min_z), c[0]) &&
                project_box_vert(to_render_space(m.max_x, m.min_y, m.min_z), c[1]) &&
                project_box_vert(to_render_space(m.max_x, m.max_y, m.min_z), c[2]) &&
                project_box_vert(to_render_space(m.min_x, m.max_y, m.min_z), c[3]) &&
                project_box_vert(to_render_space(m.min_x, m.min_y, m.max_z), c[4]) &&
                project_box_vert(to_render_space(m.max_x, m.min_y, m.max_z), c[5]) &&
                project_box_vert(to_render_space(m.max_x, m.max_y, m.max_z), c[6]) &&
                project_box_vert(to_render_space(m.min_x, m.max_y, m.max_z), c[7]);
            if (!ok) continue;
            // 12 edges of the box: bottom square, top square, 4 uprights.
            const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},
                                      {4,5},{5,6},{6,7},{7,4},
                                      {0,4},{1,5},{2,6},{3,7}};
            for (const auto& e : edges) {
                fb.draw_line(c[e[0]], c[e[1]], box_color);
            }
        }

        // --- present ---
        SDL_UpdateTexture(texture, nullptr, fb.color().data(), kWidth * 4);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);

        // One-frame capture: lets the render be inspected without a live
        // session, and makes visual checks reproducible.
        if (!screenshot.empty() && frame >= kSettleFrames) {
            std::ofstream out(screenshot, std::ios::binary);
            out << "P6\n" << kWidth << " " << kHeight << "\n255\n";
            const auto px = fb.color();
            for (int i = 0; i < kWidth * kHeight; ++i) {
                out.put(static_cast<char>(px[i * 4 + 0]));
                out.put(static_cast<char>(px[i * 4 + 1]));
                out.put(static_cast<char>(px[i * 4 + 2]));
            }
            std::cout << "wrote " << screenshot << "\n";
            running = false;
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "walk_odm: " << mesh.vertices.size() << " verts, "
              << mesh.indices.size() / 3 << " tris rendered per frame\n";
    return 0;
}
