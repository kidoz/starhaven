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
#include "core/lod/game_lod_archive.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/math3d.hpp"
#include "core/render/rasterizer.hpp"
#include "core/render/terrain_mesh.hpp"
#include "core/render/texture.hpp"
#include "core/render/tile_set.hpp"
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

// Populate `out` with the ground textures this map's tilemap actually
// references. Returns the number of distinct tiles resolved, or -1 if the
// archives could not be opened.
//
// Only the indices present in the tilemap are decoded: the global table has
// 882 records, of which a single map typically uses fewer than a hundred.
int load_ground_tiles(const openmm6::world::OdmTerrain& terrain,
                      openmm6::render::TileSet& out) {
    namespace lod = openmm6::lod;
    namespace img = openmm6::image;
    namespace world = openmm6::world;

    const auto install = openmm6::platform::install_from_env();
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

        openmm6::render::Texture tex;
        if (!openmm6::render::Texture::create(bmp.width, bmp.height,
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
int load_model_textures(const std::vector<openmm6::world::OdmModelMesh>& meshes,
                        std::map<std::string, openmm6::render::Texture>& out) {
    namespace lod = openmm6::lod;
    namespace img = openmm6::image;

    const auto install = openmm6::platform::install_from_env();
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
            openmm6::render::Texture tex;
            if (!openmm6::render::Texture::create(bmp.width, bmp.height,
                                                  std::move(bmp.rgba), tex)) {
                continue;
            }
            out.emplace(f.texture_name, std::move(tex));
            ++resolved;
        }
    }
    return resolved;
}

// MM6 world space is X/Y-horizontal with Z up; the renderer is Y-up. Model
// geometry is stored in absolute world units on the same scale as the terrain
// (verified in docs/formats/odm-model-facets.md), so placement is a pure axis
// swap with no offset.
openmm6::render::Vec3 to_render_space(std::int32_t x, std::int32_t y, std::int32_t z) {
    return {static_cast<float>(x), static_cast<float>(z), static_cast<float>(y)};
}

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
    std::string map_name;
    std::string screenshot;  // when set, render one frame to PPM and exit
    bool show_boxes = false;  // model bounding-box wireframe overlay
    // Camera defaults: above the map center, looking across it.
    openmm6::render::Vec3 start_pos{0, 32.0f * 30.0f, 0};
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
            start_yaw = openmm6::render::radians(yp[0]);
            start_pitch = openmm6::render::radians(yp[1]);
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
    const std::string title = "openmm6 — walk — " + map_name;
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
        if (!screenshot.empty()) {
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
