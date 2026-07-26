#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/assets/asset_cache.hpp"
#include "core/image/bitmap.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/tile_set.hpp"
#include "core/world/collision.hpp"
#include "core/world/decoration_table.hpp"
#include "core/world/map_event.hpp"
#include "core/world/monster_list.hpp"
#include "core/world/odm_map.hpp"
#include "core/world/sound_table.hpp"
#include "core/world/sprite_frame_table.hpp"
#include "core/world/tile_table.hpp"

#include "walker_ambient.hpp"
#include "walker_common.hpp"
#include "walker_music.hpp"
#include "walker_sprites.hpp"

namespace {

using namespace starhaven;
namespace tools = starhaven::tools;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Sprite pixels are not world units and no table states the scale, so both are
// calibrated by eye against the models. `inferred`
constexpr float kDecorationScale = 4.0f;
constexpr float kActorScale = 1.2f;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.odm>\n"
              << "\n"
              << "Loads one .odm outdoor map from your own legal game install\n"
              << "(Games.lod) and renders it as a walkable 3D world.\n"
              << "\n"
              << "Controls:\n"
              << "  W/A/S/D    move forward/left/back/right\n"
              << "  Q/E        descend/ascend (only with --fly)\n"
              << "  Shift      move faster\n"
              << "  Mouse      look around\n"
              << "  Arrows     look left/right/up/down\n"
              << "  ESC/close  quit\n"
              << "\n"
              << "  --pos X,Y,Z         start position (renderer axes, Y up)\n"
              << "  --look YAW,PITCH    start orientation in degrees\n"
              << "  --screenshot FILE   render one frame to a PPM and exit\n"
              << "  --boxes             overlay model bounding boxes\n"
              << "  --fly               disable gravity and collision\n"
              << "  --no-music          do not play the map's music track\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

// Ground textures for the tile indices this map actually uses, resolved through
// DTILE.BIN (see docs/formats/dtile.md).
int load_ground_tiles(const world::OdmTerrain& terrain, render::TileSet& out) {
    const auto install = platform::install_from_env();
    if (!install)
        return -1;

    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        return -1;
    }
    lod::LodArchive bitmaps;
    if (lod::LodArchive::open(*install / "data" / "BITMAPS.LOD", bitmaps) != lod::LodError::None) {
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

    std::array<bool, 256> used{};
    for (std::uint8_t t : terrain.tilemap)
        used[t] = true;

    int resolved = 0;
    for (int i = 0; i < 256; ++i) {
        if (!used[static_cast<std::size_t>(i)])
            continue;
        const auto* rec = table.at(static_cast<std::uint8_t>(i));
        // An empty name is a reserved slot, not an error: the shipped table has
        // more rows than art.
        if (rec == nullptr || rec->name.empty())
            continue;
        // The tile set owns its textures, so decode straight into it rather
        // than copying out of the shared cache.
        std::span<const std::byte> raw;
        if (bitmaps.payload(rec->name, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        image::Bitmap bmp;
        if (image::decode_bitmap(raw, bmp) != image::BitmapError::None)
            continue;
        render::Texture tex;
        if (!render::Texture::create(bmp.width, bmp.height, std::move(bmp.rgba), tex)) {
            continue;
        }
        if (out.set(static_cast<std::uint8_t>(i), std::move(tex)))
            ++resolved;
    }
    return resolved;
}

// Height of the terrain under a renderer-space point, interpolated across the
// cell so walking a slope is smooth rather than stepped.
float terrain_height_at(const world::OdmTerrain& terrain, render::TerrainScale scale, float x,
                        float z) {
    constexpr int dim = world::OdmTerrain::kGridDim;
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

}  // namespace

int main(int argc, char** argv) {
    std::string map_name;
    std::string screenshot;
    bool show_boxes = false;
    bool fly = false;
    bool music_wanted = true;
    bool have_pos = false;
    render::Camera camera;
    camera.position = {0, 32.0f * 30.0f, 0};
    camera.yaw = 0.6f;
    camera.pitch = -0.3f;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (a == "--boxes") {
            show_boxes = true;
        } else if (a == "--fly") {
            fly = true;
        } else if (a == "--no-music") {
            music_wanted = false;
        } else if (a == "--pos" && i + 1 < argc) {
            float xyz[3] = {0, 0, 0};
            if (tools::parse_floats(argv[++i], xyz, 3) != 3) {
                print_usage(argv[0]);
                return 2;
            }
            camera.position = {xyz[0], xyz[1], xyz[2]};
            have_pos = true;
        } else if (a == "--look" && i + 1 < argc) {
            float yp[2] = {0, 0};
            if (tools::parse_floats(argv[++i], yp, 2) != 2) {
                print_usage(argv[0]);
                return 2;
            }
            camera.yaw = render::radians(yp[0]);
            camera.pitch = render::radians(yp[1]);
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
    (void)have_pos;

    lod::GameLodArchive archive;
    if (lod::GameLodArchive::open(tools::resolve_games_lod(), archive) != lod::GameLodError::None) {
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

    assets::AssetCache cache;
    if (const auto install = platform::install_from_env()) {
        cache.open(*install / "data");
    }

    const render::TerrainMesh mesh = render::build_terrain_mesh(terrain, {});

    render::TileSet tiles;
    if (load_ground_tiles(terrain, tiles) <= 0) {
        std::cerr << "note: no ground tiles resolved; using placeholders\n";
        tiles = render::TileSet::make_placeholder();
    }

    std::vector<world::OdmModel> models;
    if (world::extract_models(map, models) != world::OdmError::None) {
        models.clear();
    }
    std::vector<world::OdmModelMesh> meshes;
    if (world::extract_model_meshes(map, meshes) != world::OdmError::None) {
        std::cerr << "note: model geometry did not decode\n";
        meshes.clear();
    }
    std::vector<world::OdmDecoration> decorations;
    if (world::extract_decorations(map, decorations) != world::OdmError::None) {
        decorations.clear();
    }

    // The sprite frame table turns an animation name into the frames to draw
    // and the size to draw them at (docs/formats/dsft.md). Decorations and
    // monsters both name animations rather than pictures.
    world::SpriteFrameTable sprite_frames;
    world::SoundTable sound_table;
    world::DecorationTable decoration_table;
    if (const auto install = platform::install_from_env()) {
        lod::LodArchive icons;
        if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) == lod::LodError::None) {
            std::span<const std::byte> raw;
            if (icons.payload("DSFT.BIN", raw) == lod::LodArchive::PayloadError::None &&
                world::SpriteFrameTable::parse(raw, sprite_frames) !=
                    world::SpriteFrameError::None) {
                sprite_frames = world::SpriteFrameTable{};
            }
            if (icons.payload("DSOUNDS.BIN", raw) == lod::LodArchive::PayloadError::None &&
                world::SoundTable::parse(raw, sound_table) != world::SoundTableError::None) {
                sound_table = world::SoundTable{};
            }
            if (icons.payload("DDECLIST.BIN", raw) == lod::LodArchive::PayloadError::None &&
                world::DecorationTable::parse(raw, decoration_table) !=
                    world::DecorationTableError::None) {
                decoration_table = world::DecorationTable{};
            }
        }
    }

    // Actors: the map's event file names the monsters standing on it, and the
    // monster table turns each id into animation names.
    std::vector<world::MapActor> actors;
    std::map<std::size_t, std::string> actor_animation;
    {
        std::string stem = map_name;
        if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        std::span<const std::byte> ev_entry;
        world::MapEventFile ev;
        if (archive.payload(stem + ".ddm", ev_entry) == lod::GameLodArchive::PayloadError::None &&
            world::parse_map_event(ev_entry, ev) == world::MapEventError::None) {
            actors = world::extract_actors(ev);
        }

        world::MonsterList monsters;
        if (const auto install = platform::install_from_env()) {
            lod::LodArchive icons;
            std::span<const std::byte> raw;
            if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) ==
                    lod::LodError::None &&
                icons.payload("DMONLIST.BIN", raw) == lod::LodArchive::PayloadError::None &&
                world::MonsterList::parse(raw, monsters) != world::MonsterListError::None) {
                monsters = world::MonsterList{};
            }
        }

        for (std::size_t i = 0; i < actors.size(); ++i) {
            std::string animation =
                tools::actor_animation(monsters, sprite_frames, cache, actors[i].monster_id);
            if (!animation.empty()) {
                actor_animation[i] = std::move(animation);
            }
        }
    }

    // Collision: the models' own facets. Terrain is sampled instead, which is
    // both cheaper and exactly right.
    world::CollisionWorld collision;
    {
        std::vector<render::Vec3> corners;
        for (const auto& m : meshes) {
            for (const auto& f : m.facets) {
                if (f.vertex_count < 3)
                    continue;
                corners.clear();
                for (std::size_t k = 0; k < f.vertex_count; ++k) {
                    const auto& v = m.vertices[f.vertex_ids[k]];
                    corners.push_back(tools::to_render_space(v.x, v.y, v.z));
                }
                collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
            }
        }
    }

    // A few decoration types make a noise; DDECLIST names which sound
    // (docs/formats/dsounds.md).
    std::vector<tools::AmbientSource> ambient_sources;
    for (const auto& d : decorations) {
        const auto* type = decoration_table.at(d.kind);
        if (type == nullptr || type->sound_id == 0)
            continue;
        ambient_sources.push_back({tools::to_render_space(d.x, d.y, d.z), type->sound_id});
    }

    // The design table names the map and picks its music (docs/formats/text-tables.md).
    const tools::MapIdentity identity = tools::identify_map(map_name);

    std::cout << map_name;
    if (!identity.display_name.empty())
        std::cout << " \"" << identity.display_name << "\"";
    std::cout << ": " << meshes.size() << " model meshes, " << collision.size()
              << " collision polygons, " << decorations.size() << " decorations, " << actors.size()
              << " actors (" << actor_animation.size() << " with sprites), "
              << sprite_frames.group_count() << " animations\n";

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title =
        "StarHaven - walk - " +
        (identity.display_name.empty() ? map_name : identity.display_name + " (" + map_name + ")");
    SDL_Window* window = SDL_CreateWindow(title.c_str(), kWidth, kHeight, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* screen = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ABGR8888,
                                            SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);

    const bool mouse_look = screenshot.empty();
    if (mouse_look) {
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    // A one-frame capture ends before a note sounds, so do not start audio for
    // it at all.
    tools::AmbientMixer ambient;
    if (!ambient_sources.empty()) {
        std::cout << ambient_sources.size() << " decorations make a sound\n";
        // A one-frame capture ends before anything is heard, so do not open a
        // device for it.
        if (screenshot.empty()) {
            if (const auto install = platform::install_from_env()) {
                (void)ambient.open(*install);
            }
        }
    }

    tools::MusicPlayer music;
    if (music_wanted && screenshot.empty() && identity.music_track > 0) {
        if (const auto install = platform::install_from_env()) {
            if (music.start(*install, identity.music_track)) {
                std::cout << "playing track " << identity.music_track << "\n";
            }
        }
    }

    const render::Vec3 sun = render::normalize(render::Vec3{0.4f, 1.0f, 0.3f});
    render::SceneRenderer scene(kWidth, kHeight);
    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    while (running) {
        ++frame;
        music.update();
        ambient.update(camera.position, ambient_sources, sound_table);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_look) {
                camera.yaw += event.motion.xrel * tools::kMouseSensitivity;
                camera.pitch -= event.motion.yrel * tools::kMouseSensitivity;
            }
        }

        const auto* keys = SDL_GetKeyboardState(nullptr);
        tools::MoveInput in;
        in.forward = keys[SDL_SCANCODE_W];
        in.back = keys[SDL_SCANCODE_S];
        in.left = keys[SDL_SCANCODE_A];
        in.right = keys[SDL_SCANCODE_D];
        in.down = keys[SDL_SCANCODE_Q];
        in.up = keys[SDL_SCANCODE_E];
        in.speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;

        tools::step_player(camera, fall_speed, fly, in, collision,
                           [&](float x, float z) { return terrain_height_at(terrain, {}, x, z); });

        if (keys[SDL_SCANCODE_LEFT])
            camera.yaw -= tools::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_RIGHT])
            camera.yaw += tools::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_UP])
            camera.pitch += tools::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_DOWN])
            camera.pitch -= tools::kLookSpeed * in.dt;
        camera.pitch =
            std::clamp(camera.pitch, -render::Camera::kMaxPitch, render::Camera::kMaxPitch);

        scene.begin(camera, {135, 180, 220, 255});  // sky

        // Terrain.
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            std::array<render::Vec3, 3> w{};
            std::array<render::Vec2, 3> uv{};
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t vi = mesh.indices[i + static_cast<std::size_t>(k)];
                w[static_cast<std::size_t>(k)] = mesh.vertices[vi];
                uv[static_cast<std::size_t>(k)] = mesh.uvs[vi];
            }
            const render::Vec3 n = render::normalize((mesh.normals[mesh.indices[i]] +
                                                      mesh.normals[mesh.indices[i + 1]] +
                                                      mesh.normals[mesh.indices[i + 2]]) *
                                                     (1.0f / 3.0f));
            float lambert = render::dot(n, sun);
            lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.8f + 0.2f;
            // UVs are in cell units, so Repeat lays one tile per cell.
            scene.draw_triangle(w, uv, lambert, tiles.texture_for(mesh.tile_ids[i / 3]),
                                render::WrapMode::Repeat, true);
        }

        // Model meshes. Backfaces are not culled: the MM6->renderer axis swap
        // mirrors the space, so on-disk winding no longer predicts screen
        // winding, and the z-buffer resolves the overdraw either way.
        for (const auto& m : meshes) {
            for (const auto& f : m.facets) {
                if (f.vertex_count < 3)
                    continue;
                const render::Vec3 n = render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
                float lambert = std::abs(render::dot(n, sun));
                lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.8f + 0.2f;

                const render::Texture& tex = cache.bitmap(f.texture_name);
                const float inv_w = tex.width() > 0 ? 1.0f / static_cast<float>(tex.width()) : 0.0f;
                const float inv_h =
                    tex.height() > 0 ? 1.0f / static_cast<float>(tex.height()) : 0.0f;

                for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
                    const std::size_t idx[3] = {0, k, k + 1};
                    std::array<render::Vec3, 3> w{};
                    std::array<render::Vec2, 3> uv{};
                    for (int c = 0; c < 3; ++c) {
                        const auto& v = m.vertices[f.vertex_ids[idx[c]]];
                        w[static_cast<std::size_t>(c)] = tools::to_render_space(v.x, v.y, v.z);
                        uv[static_cast<std::size_t>(c)] = {static_cast<float>(f.u[idx[c]]) * inv_w,
                                                           static_cast<float>(f.v[idx[c]]) * inv_h};
                    }
                    scene.draw_triangle(w, uv, lambert, tex, render::WrapMode::Repeat, false);
                }
            }
        }

        // Decorations, then actors: both are camera-facing billboards, and both
        // pick their current picture out of the sprite frame table.
        const std::uint32_t ticks = tools::sprite_ticks(SDL_GetTicks());
        for (const auto& d : decorations) {
            const tools::SpriteChoice pick = tools::choose_sprite(sprite_frames, d.name, ticks);
            const render::Texture& tex = cache.sprite(pick.entry, pick.palette);
            if (tex.empty())
                continue;
            const float size = kDecorationScale * pick.scale;
            scene.draw_billboard(tools::to_render_space(d.x, d.y, d.z),
                                 static_cast<float>(tex.width()) * size,
                                 static_cast<float>(tex.height()) * size, tex);
        }
        for (const auto& [index, animation] : actor_animation) {
            const tools::SpriteChoice pick = tools::choose_sprite(sprite_frames, animation, ticks);
            const render::Texture& tex = cache.sprite(pick.entry, pick.palette);
            if (tex.empty())
                continue;
            const auto& a = actors[index];
            const float size = kActorScale * pick.scale;
            scene.draw_billboard(tools::to_render_space(a.x, a.y, a.z),
                                 static_cast<float>(tex.width()) * size,
                                 static_cast<float>(tex.height()) * size, tex);
        }

        // Optional wireframe overlay of the model bounding boxes.
        if (show_boxes) {
            const render::Color box_color{255, 220, 0, 255};
            for (const auto& m : models) {
                std::array<render::ScreenVertex, 8> c{};
                const int corners[8][3] = {
                    {m.min_x, m.min_y, m.min_z}, {m.max_x, m.min_y, m.min_z},
                    {m.max_x, m.max_y, m.min_z}, {m.min_x, m.max_y, m.min_z},
                    {m.min_x, m.min_y, m.max_z}, {m.max_x, m.min_y, m.max_z},
                    {m.max_x, m.max_y, m.max_z}, {m.min_x, m.max_y, m.max_z}};
                bool ok = true;
                for (int k = 0; k < 8 && ok; ++k) {
                    ok = scene.project_point(
                        tools::to_render_space(corners[k][0], corners[k][1], corners[k][2]),
                        c[static_cast<std::size_t>(k)]);
                }
                if (!ok)
                    continue;
                const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto& e : edges) {
                    scene.framebuffer().draw_line(c[static_cast<std::size_t>(e[0])],
                                                  c[static_cast<std::size_t>(e[1])], box_color);
                }
            }
        }

        SDL_UpdateTexture(screen, nullptr, scene.framebuffer().color().data(), kWidth * 4);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, screen, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);

        if (!screenshot.empty() && frame >= tools::kSettleFrames) {
            if (!render::write_ppm(screenshot, scene.framebuffer())) {
                std::cerr << "error: could not write " << screenshot << "\n";
            } else {
                std::cout << "wrote " << screenshot << "\n";
            }
            running = false;
        }
    }

    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
