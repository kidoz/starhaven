#include <algorithm>
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
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/math3d.hpp"
#include "core/render/rasterizer.hpp"
#include "core/render/texture.hpp"
#include "core/world/blv_map.hpp"
#include "core/world/collision.hpp"

namespace {

// Player proportions, in MM6 world units. A terrain cell is 512 across, so a
// body a little under a third of a cell wide walks through doorways.
constexpr float kBodyRadius = 64.0f;
constexpr float kBodyHeight = 320.0f;
constexpr float kEyeHeight = 280.0f;
constexpr float kStepHeight = 96.0f;   // stairs and kerbs this tall are walked up
constexpr float kGravity = -2400.0f;   // units per second squared

constexpr float kMouseSensitivity = 0.0025f;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.blv>\n"
              << "\n"
              << "Loads one .blv indoor map from your own legal game install\n"
              << "(Games.lod) and renders its geometry as a walkable 3D level.\n"
              << "Software-rasterized (no OpenGL).\n"
              << "\n"
              << "Controls:\n"
              << "  W/A/S/D    move forward/left/back/right\n"
              << "  Q/E        descend/ascend (fly)\n"
              << "  Shift      move faster\n"
              << "  Arrows     look left/right/up/down\n"
              << "  ESC/close  quit\n"
              << "\n"
              << "  --pos X,Y,Z         start position (renderer axes, Y up)\n"
              << "  --look YAW,PITCH    start orientation in degrees\n"
              << "  --screenshot FILE   render one frame to a PPM and exit\n"
              << "  --fly               disable gravity and wall collision\n"
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

// Decode the textures this level's faces reference, keyed by face texture name.
// Returns the number resolved, or -1 if BITMAPS.LOD could not be opened.
int load_face_textures(const starhaven::world::BlvMap& map,
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
    for (const auto& f : map.faces) {
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
    return resolved;
}

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up.
starhaven::render::Vec3 to_render_space(int x, int y, int z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

bool project(const starhaven::render::Mat4& view_proj,
             starhaven::render::Vec3 world, float r, float g, float b,
             starhaven::render::Vec2 uv, starhaven::render::ScreenVertex& out) {
    using namespace starhaven::render;
    const Vec4 clip = view_proj * Vec4{world.x, world.y, world.z, 1.0f};
    if (clip.w <= 0.0001f) {
        return false;
    }
    const float inv_w = 1.0f / clip.w;
    out.x = (clip.x * inv_w * 0.5f + 0.5f) * kWidth;
    out.y = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * kHeight;
    out.z = clip.z * inv_w;
    out.r = r;
    out.g = g;
    out.b = b;
    out.u = uv.u;
    out.v = uv.v;
    out.inv_w = inv_w;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    namespace world = starhaven::world;
    namespace render = starhaven::render;
    namespace lod = starhaven::lod;

    std::string map_name;
    std::string screenshot;
    bool fly = false;
    bool have_pos = false;
    render::Vec3 start_pos{0, 0, 0};
    float start_yaw = 0.0f;
    float start_pitch = 0.0f;

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
        } else if (a == "--fly") {
            fly = true;
        } else if (a == "--pos" && i + 1 < argc) {
            float xyz[3] = {0, 0, 0};
            if (parse_floats(argv[++i], xyz, 3) != 3) {
                print_usage(argv[0]);
                return 2;
            }
            start_pos = {xyz[0], xyz[1], xyz[2]};
            have_pos = true;
        } else if (a == "--look" && i + 1 < argc) {
            float yp[2] = {0, 0};
            if (parse_floats(argv[++i], yp, 2) != 2) {
                print_usage(argv[0]);
                return 2;
            }
            start_yaw = render::radians(yp[0]);
            start_pitch = render::radians(yp[1]);
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
    if (lod::GameLodArchive::open(resolve_games_lod(), archive) !=
        lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (archive.payload(map_name, entry) !=
        lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: map not found: " << map_name << "\n";
        return 1;
    }
    world::BlvMap map;
    if (const world::BlvError e = world::parse_blv(entry, map);
        e != world::BlvError::None) {
        std::cerr << "error: could not parse BLV (code " << static_cast<int>(e)
                  << ")\n";
        return 1;
    }

    // Collision uses the same faces the renderer draws, minus the portals.
    world::CollisionWorld collision;
    {
        std::vector<render::Vec3> corners;
        for (const auto& f : map.faces) {
            if (f.invisible() || f.vertex_count < 3) continue;
            corners.clear();
            for (std::size_t k = 0; k < f.vertex_count; ++k) {
                const auto& v = map.vertices[f.vertex_ids[k]];
                corners.push_back(to_render_space(v.x, v.y, v.z));
            }
            collision.add_polygon(corners, {f.nx(), f.nz(), f.ny()});
        }
    }
    std::cout << "  collision polygons: " << collision.size() << "\n";

    std::map<std::string, render::Texture> textures;
    const int loaded = load_face_textures(map, textures);
    std::cout << map_name << ": \"" << map.header.name << "\"  "
              << map.vertices.size() << " vertices, " << map.faces.size()
              << " faces, " << (loaded > 0 ? loaded : 0) << " textures\n";

    // Without a start position, prefer the level's own "Party Start" marker:
    // that is where the game itself puts the party.
    if (!have_pos) {
        for (const auto& d : world::find_decorations(map)) {
            if (d.name == "Party Start") {
                start_pos = to_render_space(d.x, d.y, d.z) ;
                start_pos.y += kEyeHeight;
                have_pos = true;
                std::cout << "  spawning at the map's Party Start marker\n";
                break;
            }
        }
    }

    // Otherwise stand on the level's largest floor. The centre of the bounding
    // box is usually inside solid rock, so picking an upward-facing face and
    // rising a little above it lands in open space.
    if (!have_pos && !map.faces.empty()) {
        const world::BlvFace* best = nullptr;
        long best_area = -1;
        for (const auto& f : map.faces) {
            if (f.invisible() || f.vertex_count < 3) continue;
            if (f.nz() < 0.9f) continue;  // not a floor
            int minx = 0, maxx = 0, miny = 0, maxy = 0;
            for (std::size_t k = 0; k < f.vertex_count; ++k) {
                const auto& v = map.vertices[f.vertex_ids[k]];
                if (k == 0) { minx = maxx = v.x; miny = maxy = v.y; }
                minx = std::min<int>(minx, v.x); maxx = std::max<int>(maxx, v.x);
                miny = std::min<int>(miny, v.y); maxy = std::max<int>(maxy, v.y);
            }
            const long area = static_cast<long>(maxx - minx) * (maxy - miny);
            if (area > best_area) { best_area = area; best = &f; }
        }
        if (best != nullptr) {
            long sx = 0, sy = 0, sz = 0;
            for (std::size_t k = 0; k < best->vertex_count; ++k) {
                const auto& v = map.vertices[best->vertex_ids[k]];
                sx += v.x; sy += v.y; sz += v.z;
            }
            const int n = best->vertex_count;
            // Roughly eye height above the floor.
            start_pos = to_render_space(static_cast<int>(sx / n),
                                        static_cast<int>(sy / n),
                                        static_cast<int>(sz / n));
            start_pos.y += kEyeHeight;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "StarHaven — indoor — " + map_name;
    SDL_Window* window = SDL_CreateWindow(title.c_str(), kWidth, kHeight, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* screen = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        kWidth, kHeight);

    render::Vec3 cam_pos = start_pos;
    float yaw = start_yaw;
    float pitch = start_pitch;
    float fall_speed = 0.0f;

    // Relative mouse mode gives unbounded look; without a window it is a no-op,
    // which is what the screenshot path wants.
    const bool mouse_look = screenshot.empty();
    if (mouse_look) {
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    // Indoor levels have no sky, so light them from a fixed overhead direction
    // rather than a sun: it keeps floors bright and walls readable.
    const render::Vec3 lamp = render::normalize(render::Vec3{0.3f, 1.0f, 0.2f});
    render::Framebuffer fb(kWidth, kHeight);

    // A capture taken on the first frame shows the camera before gravity has
    // settled it, which misrepresents where the player actually stands.
    constexpr int kSettleFrames = 90;
    int frame = 0;

    bool running = true;
    while (running) {
        ++frame;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN &&
                     event.key.key == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_look) {
                yaw += event.motion.xrel * kMouseSensitivity;
                pitch -= event.motion.yrel * kMouseSensitivity;
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        const float speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;
        const float dt = 1.0f / 60.0f;

        const render::Vec3 fwd = render::camera_forward(yaw, pitch);
        const render::Vec3 fwd_flat = render::normalize(render::Vec3{fwd.x, 0, fwd.z});
        const render::Vec3 right = render::camera_right(yaw);

        // Horizontal intent first, then collision, then gravity: keeping them
        // separate is what lets a blocked step still slide along the wall.
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
            // Feet are eye height below the camera; collide the body, not the eye.
            const render::Vec3 feet_from{cam_pos.x, cam_pos.y - kEyeHeight, cam_pos.z};
            const render::Vec3 feet_to{wish.x, wish.y - kEyeHeight, wish.z};
            render::Vec3 feet =
                collision.slide(feet_from, feet_to, kBodyRadius, kBodyHeight);

            fall_speed += kGravity * dt;
            feet.y += fall_speed * dt;

            float ground = 0.0f;
            if (collision.floor_below({feet.x, feet.y + kStepHeight, feet.z}, ground) &&
                feet.y <= ground) {
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

        fb.clear({0, 0, 0, 255});  // indoors: no sky
        fb.clear_depth(1.0f);

        const render::Vec3 target = cam_pos + fwd;
        const render::Mat4 view = render::mat4_look_at(cam_pos, target, {0, 1, 0});
        const float aspect = static_cast<float>(kWidth) / kHeight;
        const render::Mat4 proj = render::mat4_perspective(render::radians(60.0f),
                                                           aspect, 1.0f, 20000.0f);

        auto draw_world_triangle = [&](const render::Vec3 w[3],
                                       const render::Vec2 uv[3], float shade,
                                       const render::Texture& texture) {
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
                    if (!project(proj,
                                 {clipped[t + k].x, clipped[t + k].y, clipped[t + k].z},
                                 clipped[t + k].r, clipped[t + k].g, clipped[t + k].b,
                                 {clipped[t + k].u, clipped[t + k].v}, s[k])) {
                        ok = false; break;
                    }
                }
                if (!ok) continue;
                // Faces are one-sided in the data but the axis swap mirrors
                // screen winding, so let the z-buffer sort them out.
                if (texture.empty()) {
                    fb.draw_triangle(s[0], s[1], s[2], /*cull_backfaces*/ false);
                } else {
                    fb.draw_triangle_textured(s[0], s[1], s[2], texture,
                                              render::WrapMode::Repeat, false);
                }
            }
        };

        for (const auto& f : map.faces) {
            // Attribute bit 0 marks portals and other geometry the original
            // engine never drew; drawing them would wall off every room.
            if (f.invisible() || f.vertex_count < 3) continue;

            const render::Vec3 n =
                render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
            float lambert = std::abs(render::dot(n, lamp));
            lambert = std::clamp(lambert, 0.0f, 1.0f) * 0.7f + 0.3f;

            const auto it = textures.find(f.texture_name);
            const bool has_tex = it != textures.end();
            const float inv_w = has_tex && it->second.width() > 0
                ? 1.0f / static_cast<float>(it->second.width()) : 0.0f;
            const float inv_h = has_tex && it->second.height() > 0
                ? 1.0f / static_cast<float>(it->second.height()) : 0.0f;

            auto corner = [&](std::size_t k) {
                const auto& v = map.vertices[f.vertex_ids[k]];
                return to_render_space(v.x, v.y, v.z);
            };
            auto corner_uv = [&](std::size_t k) {
                return render::Vec2{static_cast<float>(f.u[k]) * inv_w,
                                    static_cast<float>(f.v[k]) * inv_h};
            };

            static const render::Texture kNoTexture;
            for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
                const render::Vec3 w[3] = {corner(0), corner(k), corner(k + 1)};
                const render::Vec2 uv[3] = {corner_uv(0), corner_uv(k),
                                            corner_uv(k + 1)};
                draw_world_triangle(w, uv, lambert,
                                    has_tex ? it->second : kNoTexture);
            }
        }

        SDL_UpdateTexture(screen, nullptr, fb.color().data(), kWidth * 4);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, screen, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);

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

    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
