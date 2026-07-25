#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/math3d.hpp"
#include "core/render/rasterizer.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/texture.hpp"
#include "core/render/tile_set.hpp"
#include "core/world/odm_map.hpp"

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
              << "\n"
              << "Set " << openmm6::platform::kInstallEnvVar
              << " to the install directory.\n";
}

std::filesystem::path resolve_games_lod() {
    namespace fs = std::filesystem;
    if (auto install = openmm6::platform::install_from_env()) {
        fs::path p = *install / "data" / "Games.lod";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "data/Games.lod";
}

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Transform one world-space vertex through view+projection, then to a screen
// vertex. Returns false if the vertex is behind the near plane (caller should
// have clipped already).
bool project(const openmm6::render::Mat4& view_proj,
             openmm6::render::Vec3 world, float r, float g, float b,
             openmm6::render::Vec2 uv, openmm6::render::ScreenVertex& out) {
    using namespace openmm6::render;
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
    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string map_name = argv[1];

    namespace lod = openmm6::lod;
    namespace world = openmm6::world;
    namespace render = openmm6::render;

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

    // Generated stand-in ground textures, not MM6 art: the tile-index to
    // ground-bitmap mapping is still unresolved (docs/rendering/
    // terrain-coloring.md). Swapping this one line for the real loader is all
    // that stands between this view and actual game textures.
    const render::TileSet tiles = render::TileSet::make_placeholder();

    std::vector<world::OdmModel> models;
    if (world::extract_models(map, models) != world::OdmError::None) {
        models.clear();
    }
    // The first model's mesh vertices (a verified subset; subsequent models
    // need the variable-length facet stream decoded first).
    std::vector<world::OdmModelVertex> model_verts;
    if (world::extract_first_model_vertices(map, model_verts) != world::OdmError::None) {
        model_verts.clear();
    }

    // SDL3 returns true on success, unlike SDL2's 0-on-success convention.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "openmm6 — walk — " + map_name;
    // SDL3 drops the x/y arguments and SDL_WINDOW_SHOWN; nullptr picks the
    // default render backend.
    SDL_Window* window = SDL_CreateWindow(title.c_str(), kWidth, kHeight, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        kWidth, kHeight);

    // Camera state: positioned above the map center, looking across.
    render::Vec3 cam_pos{0, 32.0f * 30.0f, 0};  // height ~ above the field
    float yaw = 0.6f;   // radians
    float pitch = -0.3f;

    const render::Vec3 sun = render::normalize(render::Vec3{0.4f, 1.0f, 0.3f});
    render::Framebuffer fb(kWidth, kHeight);

    bool running = true;
    while (running) {
        // --- input ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }
        // SDL3 hands back `const bool*` here; SDL2 used `const Uint8*`.
        const auto* keys = SDL_GetKeyboardState(nullptr);
        const float speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;
        const float dt = 1.0f / 60.0f;

        const render::Vec3 fwd = render::camera_forward(yaw, pitch);
        const render::Vec3 fwd_flat = render::normalize(render::Vec3{fwd.x, 0, fwd.z});
        const render::Vec3 right = render::camera_right(yaw);
        if (keys[SDL_SCANCODE_W]) cam_pos = cam_pos + fwd_flat * (speed * dt);
        if (keys[SDL_SCANCODE_S]) cam_pos = cam_pos - fwd_flat * (speed * dt);
        if (keys[SDL_SCANCODE_A]) cam_pos = cam_pos - right * (speed * dt);
        if (keys[SDL_SCANCODE_D]) cam_pos = cam_pos + right * (speed * dt);
        if (keys[SDL_SCANCODE_Q]) cam_pos.y -= speed * dt;
        if (keys[SDL_SCANCODE_E]) cam_pos.y += speed * dt;
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

            // View-space coords for near clipping.
            const render::Vec4 vp[3] = {view * render::Vec4{w0.x, w0.y, w0.z, 1},
                                        view * render::Vec4{w1.x, w1.y, w1.z, 1},
                                        view * render::Vec4{w2.x, w2.y, w2.z, 1}};
            // Flat shading: the scalar Lambert term drives all three channels,
            // so terrain stays grayscale-modulated as before.
            render::ViewVertex vv[3] = {
                {vp[0].x, vp[0].y, vp[0].z, lambert, lambert, lambert, t0.u, t0.v},
                {vp[1].x, vp[1].y, vp[1].z, lambert, lambert, lambert, t1.u, t1.v},
                {vp[2].x, vp[2].y, vp[2].z, lambert, lambert, lambert, t2.u, t2.v}};
            std::vector<render::ViewVertex> clipped;
            render::clip_near(vv, /*near_z*/ -1.0f, clipped);
            if (clipped.empty()) continue;

            const render::Texture& tile = tiles.texture_for(tile_id);

            // Project and rasterize each resulting triangle.
            for (std::size_t t = 0; t + 2 < clipped.size(); t += 3) {
                render::ScreenVertex s[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    // `clipped` is already in view space, so only the
                    // projection matrix may be applied here. Using view_proj
                    // would transform by the view matrix a second time.
                    if (!project(proj,
                                 {clipped[t + k].x, clipped[t + k].y, clipped[t + k].z},
                                 clipped[t + k].r, clipped[t + k].g,
                                 clipped[t + k].b,
                                 {clipped[t + k].u, clipped[t + k].v}, s[k])) {
                        ok = false; break;
                    }
                }
                if (!ok) continue;
                // UVs are in cell units, so Repeat lays one tile per cell.
                fb.draw_triangle_textured(s[0], s[1], s[2], tile,
                                          render::WrapMode::Repeat,
                                          /*cull_backfaces*/ true);
            }
        }

        // Overlay model bounding boxes as wireframe (debug placement of props).
        // Model coordinates are in MM6 world units, matching the terrain scale.
        const render::Color box_color{255, 220, 0, 255};
        // Debug overlays are drawn with draw_line/draw_point, which ignore UVs.
        auto project_box_vert = [&](render::Vec3 world, render::ScreenVertex& out) {
            return project(view_proj, world, 1.0f, 1.0f, 1.0f, {0.0f, 0.0f}, out);
        };
        for (const auto& m : models) {
            render::ScreenVertex c[8];
            const bool ok =
                project_box_vert({static_cast<float>(m.min_x), static_cast<float>(m.min_y), static_cast<float>(m.min_z)}, c[0]) &&
                project_box_vert({static_cast<float>(m.max_x), static_cast<float>(m.min_y), static_cast<float>(m.min_z)}, c[1]) &&
                project_box_vert({static_cast<float>(m.max_x), static_cast<float>(m.min_y), static_cast<float>(m.max_z)}, c[2]) &&
                project_box_vert({static_cast<float>(m.min_x), static_cast<float>(m.min_y), static_cast<float>(m.max_z)}, c[3]) &&
                project_box_vert({static_cast<float>(m.min_x), static_cast<float>(m.max_y), static_cast<float>(m.min_z)}, c[4]) &&
                project_box_vert({static_cast<float>(m.max_x), static_cast<float>(m.max_y), static_cast<float>(m.min_z)}, c[5]) &&
                project_box_vert({static_cast<float>(m.max_x), static_cast<float>(m.max_y), static_cast<float>(m.max_z)}, c[6]) &&
                project_box_vert({static_cast<float>(m.min_x), static_cast<float>(m.max_y), static_cast<float>(m.max_z)}, c[7]);
            if (!ok) continue;
            // 12 edges of the box: bottom square, top square, 4 uprights.
            const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},
                                      {4,5},{5,6},{6,7},{7,4},
                                      {0,4},{1,5},{2,6},{3,7}};
            for (const auto& e : edges) {
                fb.draw_line(c[e[0]], c[e[1]], box_color);
            }
        }

        // Overlay the first model's mesh vertices as bright cyan points.
        const render::Color vert_color{0, 240, 255, 255};
        for (const auto& v : model_verts) {
            render::ScreenVertex sv;
            if (project_box_vert({static_cast<float>(v.x), static_cast<float>(v.y),
                                  static_cast<float>(v.z)}, sv)) {
                fb.draw_point(sv, vert_color);
            }
        }

        // --- present ---
        SDL_UpdateTexture(texture, nullptr, fb.color().data(), kWidth * 4);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "walk_odm: " << mesh.vertices.size() << " verts, "
              << mesh.indices.size() / 3 << " tris rendered per frame\n";
    return 0;
}
