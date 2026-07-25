#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/assets/asset_cache.hpp"
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/collision.hpp"
#include "core/world/sprite_frame_table.hpp"

#include "walker_common.hpp"
#include "walker_music.hpp"
#include "walker_sprites.hpp"

namespace {

using namespace starhaven;
namespace tools = starhaven::tools;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Decoration sprites are drawn well above 1:1; see docs/formats/odm-decorations.md.
constexpr float kSpriteScale = 4.0f;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.blv>\n"
              << "\n"
              << "Loads one .blv indoor map from your own legal game install\n"
              << "(Games.lod) and renders it as a walkable 3D level.\n"
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
              << "  --fly               disable gravity and wall collision\n"
              << "  --no-music          do not play the map's music track\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_name;
    std::string screenshot;
    bool fly = false;
    bool music_wanted = true;
    bool have_pos = false;
    render::Camera camera;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (a == "--fly") {
            fly = true;
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
    world::BlvMap map;
    if (const world::BlvError e = world::parse_blv(entry, map); e != world::BlvError::None) {
        std::cerr << "error: could not parse BLV (code " << static_cast<int>(e) << ")\n";
        return 1;
    }

    assets::AssetCache cache;
    if (const auto install = platform::install_from_env()) {
        cache.open(*install / "data");
    }

    // Collision uses the same faces the renderer draws, minus the portals.
    world::CollisionWorld collision;
    {
        std::vector<render::Vec3> corners;
        for (const auto& f : map.faces) {
            if (f.invisible() || f.vertex_count < 3)
                continue;
            corners.clear();
            for (std::size_t k = 0; k < f.vertex_count; ++k) {
                const auto& v = map.vertices[f.vertex_ids[k]];
                corners.push_back(tools::to_render_space(v.x, v.y, v.z));
            }
            collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
        }
    }

    const auto decorations = world::find_decorations(map);

    // Torches and braziers are animations, not pictures; the frame table says
    // which sprite each shows now (docs/formats/dsft.md).
    world::SpriteFrameTable sprite_frames;
    if (const auto install = platform::install_from_env()) {
        lod::LodArchive icons;
        std::span<const std::byte> raw;
        if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) == lod::LodError::None &&
            icons.payload("DSFT.BIN", raw) == lod::LodArchive::PayloadError::None &&
            world::SpriteFrameTable::parse(raw, sprite_frames) != world::SpriteFrameError::None) {
            sprite_frames = world::SpriteFrameTable{};
        }
    }

    // The design table names the map and picks its music (docs/formats/text-tables.md).
    // The level's own header carries a name too, but it is often a placeholder
    // the designers never filled in: D01.blv calls itself "No Name Level" where
    // the table calls it "Goblinwatch".
    const tools::MapIdentity identity = tools::identify_map(map_name);

    std::cout << map_name;
    if (!identity.display_name.empty())
        std::cout << " \"" << identity.display_name << "\"";
    std::cout << ": header \"" << map.header.name << "\"  " << map.vertices.size() << " vertices, "
              << map.faces.size() << " faces, " << collision.size() << " collision polygons, "
              << decorations.size() << " decorations\n";

    // Without a start position, prefer the level's own "Party Start" marker:
    // that is where the game itself puts the party.
    if (!have_pos) {
        for (const auto& d : decorations) {
            if (d.name == "Party Start") {
                camera.position = tools::to_render_space(d.x, d.y, d.z);
                camera.position.y += tools::kEyeHeight;
                have_pos = true;
                std::cout << "  spawning at the map's Party Start marker\n";
                break;
            }
        }
    }
    // Otherwise stand on the level's largest floor. The centre of the bounding
    // box is usually inside solid rock.
    if (!have_pos && !map.faces.empty()) {
        const world::BlvFace* best = nullptr;
        long best_area = -1;
        for (const auto& f : map.faces) {
            if (f.invisible() || f.vertex_count < 3 || f.nz() < 0.9f)
                continue;
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
        if (best != nullptr) {
            long sx = 0, sy = 0, sz = 0;
            for (std::size_t k = 0; k < best->vertex_count; ++k) {
                const auto& v = map.vertices[best->vertex_ids[k]];
                sx += v.x;
                sy += v.y;
                sz += v.z;
            }
            const int n = best->vertex_count;
            camera.position = tools::to_render_space(
                static_cast<int>(sx / n), static_cast<int>(sy / n), static_cast<int>(sz / n));
            camera.position.y += tools::kEyeHeight;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title =
        "StarHaven - indoor - " +
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
    tools::MusicPlayer music;
    if (music_wanted && screenshot.empty() && identity.music_track > 0) {
        if (const auto install = platform::install_from_env()) {
            if (music.start(*install, identity.music_track)) {
                std::cout << "playing track " << identity.music_track << "\n";
            }
        }
    }

    // Indoor levels have no sky, so light them from a fixed overhead direction.
    const render::Vec3 lamp = render::normalize(render::Vec3{0.3f, 1.0f, 0.2f});
    render::SceneRenderer scene(kWidth, kHeight);
    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    while (running) {
        ++frame;
        music.update();
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

        // Indoors there is no heightfield, only the level's own polygons.
        tools::step_player(camera, fall_speed, fly, in, collision,
                           [](float, float) { return -1.0e9f; });

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

        scene.begin(camera, {0, 0, 0, 255});  // indoors: no sky

        for (const auto& f : map.faces) {
            // Attribute bit 0 marks portals the original engine never drew;
            // drawing them would wall off every room.
            if (f.invisible() || f.vertex_count < 3)
                continue;

            const render::Vec3 n = render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
            float lambert = std::abs(render::dot(n, lamp));
            lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.7f + 0.3f;

            const render::Texture& tex = cache.bitmap(f.texture_name);
            const float inv_w = tex.width() > 0 ? 1.0f / static_cast<float>(tex.width()) : 0.0f;
            const float inv_h = tex.height() > 0 ? 1.0f / static_cast<float>(tex.height()) : 0.0f;

            for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
                const std::size_t idx[3] = {0, k, k + 1};
                std::array<render::Vec3, 3> w{};
                std::array<render::Vec2, 3> uv{};
                for (int c = 0; c < 3; ++c) {
                    const auto& v = map.vertices[f.vertex_ids[idx[c]]];
                    w[static_cast<std::size_t>(c)] = tools::to_render_space(v.x, v.y, v.z);
                    uv[static_cast<std::size_t>(c)] = {static_cast<float>(f.u[idx[c]]) * inv_w,
                                                       static_cast<float>(f.v[idx[c]]) * inv_h};
                }
                // Faces are one-sided in the data, but the axis swap mirrors
                // screen winding, so let the z-buffer sort them out.
                scene.draw_triangle(w, uv, lambert, tex, render::WrapMode::Repeat, false);
            }
        }

        // Decorations: torches, braziers and barrels standing in the level.
        const std::uint32_t ticks = tools::sprite_ticks(SDL_GetTicks());
        for (const auto& d : decorations) {
            const tools::SpriteChoice pick = tools::choose_sprite(sprite_frames, d.name, ticks);
            const render::Texture& tex = cache.sprite(pick.entry, pick.palette);
            if (tex.empty())
                continue;
            const float size = kSpriteScale * pick.scale;
            scene.draw_billboard(tools::to_render_space(d.x, d.y, d.z),
                                 static_cast<float>(tex.width()) * size,
                                 static_cast<float>(tex.height()) * size, tex);
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
