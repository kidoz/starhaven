// StarHaven — open-source engine for Might and Magic VI: The Mandate of Heaven.
//
// This is the engine's shell: it loads any of the game's 67 maps, indoor or
// outdoor, through one code path and renders it as a walkable world with its
// own music, ambient sound, monsters and loot. It never bundles game data —
// point it at your own legal install with STARHAVEN_GAME_DIR.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "config.h"
#include "core/assets/asset_cache.hpp"
#include "core/data/game_data.hpp"
#include "core/data/monster_stats.hpp"
#include "core/image/font.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/world/map_session.hpp"
#include "game/ambient_mixer.hpp"
#include "game/inspect.hpp"
#include "game/music_player.hpp"
#include "game/player.hpp"
#include "game/sprites.hpp"
#include "game/text.hpp"

namespace {

using namespace starhaven;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Sprite pixels are not world units and no table states the absolute scale, so
// these are calibrated by eye against the models. The frame table's per-sprite
// multiplier is applied on top (see docs/formats/dsft.md). `inferred`
constexpr float kDecorationScale = 4.0f;
constexpr float kActorScale = 1.2f;
constexpr float kObjectScale = 2.0f;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [<map>] [options]\n"
              << "\n"
              << "Renders one map from your own legal game install as a\n"
              << "walkable 3D world. `map` is a Games.lod entry such as\n"
              << "OutA1.Odm or D01.blv.\n"
              << "\n"
              << "Controls:\n"
              << "  W/A/S/D    move forward/left/back/right\n"
              << "  Q/E        descend/ascend (only with --fly)\n"
              << "  Shift      move faster\n"
              << "  Mouse      look around\n"
              << "  Arrows     look left/right/up/down\n"
              << "  Tab        list the establishments on this map\n"
              << "  ESC/close  quit\n"
              << "\n"
              << "  --maps              list the maps and exit\n"
              << "  --pos X,Y,Z         start position (renderer axes, Y up)\n"
              << "  --look YAW,PITCH    start orientation in degrees\n"
              << "  --screenshot FILE   render one frame to a PPM and exit\n"
              << "  --bench N           render N frames, report timings, and exit\n"
              << "  --boxes             overlay model bounding boxes\n"
              << "  --labels            name the monsters and loot in the world\n"
              << "  --fly               disable gravity and collision\n"
              << "  --no-music          do not play the map's music track\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

std::filesystem::path resolve_data_dir() {
    if (const auto install = platform::install_from_env()) {
        return *install / "data";
    }
    return "data";
}

// Every map the design table lists, which is exactly the set Games.lod ships
// (see docs/formats/text-tables.md).
int list_maps(const std::filesystem::path& data_dir) {
    data::MapStatsTable maps;
    if (data::load_map_stats(data_dir, maps) != data::GameDataError::None) {
        std::cerr << "error: could not read MapStats.txt\n";
        return 1;
    }
    std::cout << maps.size() << " maps\n";
    for (const auto& m : maps.entries()) {
        std::cout << "  " << m.file_name << "\t" << data::cp1252_to_utf8(m.name) << "\n";
    }
    return 0;
}

void draw_outdoor(render::SceneRenderer& scene, const world::MapSession& session,
                  assets::AssetCache& cache, const render::Vec3& sun) {
    const auto& mesh = session.terrain_mesh;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        std::array<render::Vec3, 3> w{};
        std::array<render::Vec2, 3> uv{};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t vi = mesh.indices[i + static_cast<std::size_t>(k)];
            w[static_cast<std::size_t>(k)] = mesh.vertices[vi];
            uv[static_cast<std::size_t>(k)] = mesh.uvs[vi];
        }
        const render::Vec3 n =
            render::normalize((mesh.normals[mesh.indices[i]] + mesh.normals[mesh.indices[i + 1]] +
                               mesh.normals[mesh.indices[i + 2]]) *
                              (1.0f / 3.0f));
        const float lambert = std::clamp(render::dot(n, sun), 0.0f, 1.0f) * 0.8f + 0.2f;
        // UVs are in cell units, so Repeat lays one tile per cell.
        scene.draw_triangle(w, uv, lambert, session.tiles.texture_for(mesh.tile_ids[i / 3]),
                            render::WrapMode::Repeat, true);
    }

    // Backfaces are not culled: the MM6->renderer axis swap mirrors the space,
    // so on-disk winding no longer predicts screen winding, and the z-buffer
    // resolves the overdraw either way.
    for (const auto& m : session.meshes) {
        for (const auto& f : m.facets) {
            if (f.vertex_count < 3) {
                continue;
            }
            const render::Vec3 n = render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
            const float lambert =
                std::clamp(std::abs(render::dot(n, sun)), 0.0f, 1.0f) * 0.8f + 0.2f;
            const render::Texture& tex = cache.bitmap(f.texture_name);
            const float inv_w = tex.width() > 0 ? 1.0f / static_cast<float>(tex.width()) : 0.0f;
            const float inv_h = tex.height() > 0 ? 1.0f / static_cast<float>(tex.height()) : 0.0f;
            for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
                const std::size_t idx[3] = {0, k, k + 1};
                std::array<render::Vec3, 3> w{};
                std::array<render::Vec2, 3> uv{};
                for (int c = 0; c < 3; ++c) {
                    const auto& v = m.vertices[f.vertex_ids[idx[c]]];
                    w[static_cast<std::size_t>(c)] = world::to_render_space(v.x, v.y, v.z);
                    uv[static_cast<std::size_t>(c)] = {static_cast<float>(f.u[idx[c]]) * inv_w,
                                                       static_cast<float>(f.v[idx[c]]) * inv_h};
                }
                scene.draw_triangle(w, uv, lambert, tex, render::WrapMode::Repeat, false);
            }
        }
    }
}

void draw_indoor(render::SceneRenderer& scene, const world::MapSession& session,
                 assets::AssetCache& cache, const render::Vec3& lamp) {
    for (const auto& f : session.blv.faces) {
        if (f.invisible() || f.vertex_count < 3) {
            continue;
        }
        const render::Vec3 n = render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
        const float lambert = std::clamp(std::abs(render::dot(n, lamp)), 0.0f, 1.0f) * 0.7f + 0.3f;

        const render::Texture& tex = cache.bitmap(f.texture_name);
        const float inv_w = tex.width() > 0 ? 1.0f / static_cast<float>(tex.width()) : 0.0f;
        const float inv_h = tex.height() > 0 ? 1.0f / static_cast<float>(tex.height()) : 0.0f;

        for (std::size_t k = 1; k + 1 < f.vertex_count; ++k) {
            const std::size_t idx[3] = {0, k, k + 1};
            std::array<render::Vec3, 3> w{};
            std::array<render::Vec2, 3> uv{};
            for (int c = 0; c < 3; ++c) {
                const auto& v = session.blv.vertices[f.vertex_ids[idx[c]]];
                w[static_cast<std::size_t>(c)] = world::to_render_space(v.x, v.y, v.z);
                uv[static_cast<std::size_t>(c)] = {static_cast<float>(f.u[idx[c]]) * inv_w,
                                                   static_cast<float>(f.v[idx[c]]) * inv_h};
            }
            scene.draw_triangle(w, uv, lambert, tex, render::WrapMode::Repeat, false);
        }
    }
}

// Decorations, monsters and loot are all camera-facing billboards that pick
// their current picture out of the sprite frame table.
void draw_billboards(render::SceneRenderer& scene, const world::MapSession& session,
                     assets::AssetCache& cache, std::uint32_t ticks) {
    auto draw = [&](const std::string& animation, const render::Vec3& position, float scale) {
        const game::SpriteChoice pick =
            game::choose_sprite(session.sprite_frames, animation, ticks);
        if (pick.entry.empty()) {
            return;
        }
        const render::Texture& tex = cache.sprite(pick.entry, pick.palette);
        if (tex.empty()) {
            return;
        }
        const float size = scale * pick.scale;
        scene.draw_billboard(position, static_cast<float>(tex.width()) * size,
                             static_cast<float>(tex.height()) * size, tex);
    };

    for (const auto& d : session.decorations) {
        draw(d.name, d.position, kDecorationScale);
    }
    for (const auto& a : session.actors) {
        draw(a.animation, a.position, kActorScale);
    }
    for (const auto& o : session.objects) {
        const auto* descriptor = session.object_descriptors.at(o.descriptor_index);
        if (descriptor == nullptr ||
            descriptor->sprite_frame_index >= session.sprite_frames.size()) {
            continue;
        }
        const auto& frame = session.sprite_frames.frames()[descriptor->sprite_frame_index];
        if (frame.group_name.empty()) {
            continue;
        }
        draw(frame.group_name, o.position, kObjectScale);
    }
}

// The game's own interface font. A missing font is not fatal: the world still
// renders, without the overlay.
image::Font load_font(const std::filesystem::path& data_dir, const char* name) {
    lod::LodArchive icons;
    image::Font font;
    std::span<const std::byte> raw;
    if (lod::LodArchive::open(data_dir / "icons.lod", icons) == lod::LodError::None &&
        icons.payload(name, raw) == lod::LodArchive::PayloadError::None) {
        (void)image::Font::parse(raw, font);
    }
    return font;
}

// Name the things standing in the world, using the game's own font. A label is
// drawn centred over its subject and only when the subject is in front of the
// camera and near enough to read.
void draw_labels(render::SceneRenderer& scene, const world::MapSession& session,
                 const image::Font& font, const render::Vec3& eye) {
    if (font.glyph_count() == 0) {
        return;
    }
    // Beyond this the text is unreadable clutter rather than information.
    constexpr float kRange = 4096.0f;
    constexpr float kLift = 200.0f;  // roughly a body height above the feet
    // A billboard writes its own depth, and a sprite's centre sits marginally
    // nearer than the point it is anchored at. This tolerance keeps a thing
    // from occluding itself.
    constexpr float kDepthSlack = 0.0005f;

    auto label = [&](const std::string& text, const render::Vec3& at, render::Color colour) {
        if (text.empty()) {
            return;
        }
        const render::Vec3 d{at.x - eye.x, at.y - eye.y, at.z - eye.z};
        if (d.x * d.x + d.y * d.y + d.z * d.z > kRange * kRange) {
            return;
        }
        render::ScreenVertex p;
        if (!scene.project_point({at.x, at.y + kLift, at.z}, p)) {
            return;
        }
        // The subject itself is drawn as a billboard, so the depth already at
        // its pixel is the subject's own unless a wall is in front. Comparing
        // against the point being labelled — not the raised label position —
        // keeps a monster's own sprite from hiding its name.
        render::ScreenVertex feet;
        if (scene.project_point(at, feet) &&
            scene.framebuffer().depth_at(static_cast<int>(feet.x), static_cast<int>(feet.y)) <
                feet.z - kDepthSlack) {
            return;
        }
        const int x = static_cast<int>(p.x) - font.text_width(text) / 2;
        game::draw_text(scene.framebuffer(), font, x, static_cast<int>(p.y), text, colour,
                        render::Color{0, 0, 0, 255});
    };

    for (const auto& a : session.actors) {
        label(a.name, a.position, render::Color{255, 200, 200, 255});
    }
    for (const auto& o : session.objects) {
        label(o.name, o.position, render::Color{200, 230, 255, 255});
    }
}

// A panel in the corner naming what the player is looking at, with a dark
// backing so the text reads against any world behind it.
void draw_panel(render::SceneRenderer& scene, const image::Font& font,
                const game::Inspected& what) {
    if (what.empty() || font.glyph_count() == 0) {
        return;
    }
    const int line_height = font.height() + 2;
    int widest = font.text_width(what.title);
    for (const auto& line : what.lines) {
        widest = std::max(widest, font.text_width(line));
    }

    constexpr int kPad = 6;
    const int rows = 1 + static_cast<int>(what.lines.size());
    const int box_w = widest + kPad * 2;
    const int box_h = rows * line_height + kPad * 2;
    const int x0 = kWidth - box_w - 8;
    const int y0 = kHeight - box_h - 8;

    auto pixels = scene.framebuffer().color();
    for (int y = y0; y < y0 + box_h; ++y) {
        for (int x = x0; x < x0 + box_w; ++x) {
            if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
                continue;
            }
            const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            // Darken rather than overwrite, so the panel sits in the scene.
            pixels[i] = static_cast<std::uint8_t>(pixels[i] / 4);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 4);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 4);
        }
    }

    int y = y0 + kPad;
    game::draw_text(scene.framebuffer(), font, x0 + kPad, y, what.title,
                    render::Color{255, 236, 170, 255}, render::Color{0, 0, 0, 255});
    y += line_height;
    for (const auto& line : what.lines) {
        game::draw_text(scene.framebuffer(), font, x0 + kPad, y, line,
                        render::Color{220, 220, 220, 255}, render::Color{0, 0, 0, 255});
        y += line_height;
    }
}

// A directory of the establishments the design table places on this map,
// drawn down the left edge. They have no position in the world, so a list is
// the honest way to show them.
void draw_directory(render::SceneRenderer& scene, const image::Font& font,
                    const world::MapSession& session) {
    if (session.buildings.empty() || font.glyph_count() == 0) {
        return;
    }
    const int line_height = font.height() + 1;
    const int rows =
        std::min<int>(static_cast<int>(session.buildings.size()), (kHeight - 60) / line_height);

    auto pixels = scene.framebuffer().color();
    const int box_h = rows * line_height + 12;
    for (int y = 30; y < 30 + box_h && y < kHeight; ++y) {
        for (int x = 0; x < kWidth / 2; ++x) {
            const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            pixels[i] = static_cast<std::uint8_t>(pixels[i] / 4);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 4);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 4);
        }
    }

    int y = 36;
    for (int i = 0; i < rows; ++i) {
        const auto& b = session.buildings[static_cast<std::size_t>(i)];
        const std::string line = b.name + "  (" + b.type + ", " + std::to_string(b.opens) + "-" +
                                 std::to_string(b.closes) + ")";
        game::draw_text(scene.framebuffer(), font, 8, y, line, render::Color{220, 220, 220, 255},
                        render::Color{0, 0, 0, 255});
        y += line_height;
    }
    if (rows < static_cast<int>(session.buildings.size())) {
        game::draw_text(
            scene.framebuffer(), font, 8, y,
            "... " + std::to_string(session.buildings.size() - static_cast<std::size_t>(rows)) +
                " more",
            render::Color{160, 160, 160, 255}, render::Color{0, 0, 0, 255});
    }
}

void draw_boxes(render::SceneRenderer& scene, const world::MapSession& session) {
    const render::Color box_color{255, 220, 0, 255};
    for (const auto& m : session.models) {
        std::array<render::ScreenVertex, 8> c{};
        const int corners[8][3] = {{m.min_x, m.min_y, m.min_z}, {m.max_x, m.min_y, m.min_z},
                                   {m.max_x, m.max_y, m.min_z}, {m.min_x, m.max_y, m.min_z},
                                   {m.min_x, m.min_y, m.max_z}, {m.max_x, m.min_y, m.max_z},
                                   {m.max_x, m.max_y, m.max_z}, {m.min_x, m.max_y, m.max_z}};
        bool ok = true;
        for (int k = 0; k < 8 && ok; ++k) {
            ok = scene.project_point(
                world::to_render_space(corners[k][0], corners[k][1], corners[k][2]),
                c[static_cast<std::size_t>(k)]);
        }
        if (!ok) {
            continue;
        }
        const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                  {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            scene.framebuffer().draw_line(c[static_cast<std::size_t>(e[0])],
                                          c[static_cast<std::size_t>(e[1])], box_color);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_name;
    std::string screenshot;
    bool show_boxes = false;
    bool fly = false;
    bool music_wanted = true;
    bool list_only = false;
    bool show_labels = false;
    bool show_directory = false;
    int bench_frames = 0;
    bool have_pos = false;
    render::Camera camera;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--maps") {
            list_only = true;
        } else if (a == "--bench" && i + 1 < argc) {
            bench_frames = std::atoi(argv[++i]);
        } else if (a == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (a == "--boxes") {
            show_boxes = true;
        } else if (a == "--labels") {
            show_labels = true;
        } else if (a == "--fly") {
            fly = true;
        } else if (a == "--no-music") {
            music_wanted = false;
        } else if (a == "--pos" && i + 1 < argc) {
            float xyz[3] = {0, 0, 0};
            if (game::parse_floats(argv[++i], xyz, 3) != 3) {
                print_usage(argv[0]);
                return 2;
            }
            camera.position = {xyz[0], xyz[1], xyz[2]};
            have_pos = true;
        } else if (a == "--look" && i + 1 < argc) {
            float yp[2] = {0, 0};
            if (game::parse_floats(argv[++i], yp, 2) != 2) {
                print_usage(argv[0]);
                return 2;
            }
            camera.yaw = render::radians(yp[0]);
            camera.pitch = render::radians(yp[1]);
        } else if (a.rfind("--", 0) == 0) {
            print_usage(argv[0]);
            return 2;
        } else if (map_name.empty()) {
            map_name = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    const std::filesystem::path data_dir = resolve_data_dir();
    if (list_only) {
        return list_maps(data_dir);
    }
    if (map_name.empty()) {
        std::cout << "StarHaven " << STARHAVEN_VERSION << "\n";
        if (const auto install = platform::install_from_env()) {
            std::cout << "Game install: " << install->string() << "\n";
        } else {
            std::cout << "No game install configured. Set " << platform::kInstallEnvVar << ".\n";
        }
        print_usage(argv[0]);
        return 2;
    }

    assets::AssetCache cache;
    cache.open(data_dir);

    const auto load_started = std::chrono::steady_clock::now();
    world::MapSession session;
    if (const world::MapSessionError e =
            world::load_map_session(game::resolve_games_lod(), data_dir, map_name, cache, session);
        e != world::MapSessionError::None) {
        std::cerr << "error: could not load " << map_name << " (code " << static_cast<int>(e)
                  << ")\n";
        return 1;
    }

    const double load_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - load_started)
            .count();

    std::cout << session.file_name << " \"" << session.title()
              << "\": " << (session.outdoor() ? "outdoor" : "indoor") << ", "
              << session.collision.size() << " collision polygons, " << session.decorations.size()
              << " decorations, " << session.actors.size() << " monsters ("
              << std::count_if(session.actors.begin(), session.actors.end(),
                               [](const auto& a) { return !a.name.empty(); })
              << " named), " << session.objects.size() << " objects ("
              << std::count_if(session.objects.begin(), session.objects.end(),
                               [](const auto& o) { return !o.name.empty(); })
              << " named)";
    if (!session.buildings.empty()) {
        std::cout << ", " << session.buildings.size() << " establishments";
    }
    std::cout << "\n";

    std::vector<game::AmbientSource> ambient_sources;
    for (const auto& d : session.decorations) {
        if (d.sound_id != 0) {
            ambient_sources.push_back({d.position, d.sound_id});
        }
    }

    if (!have_pos) {
        if (session.outdoor()) {
            camera.position = {0, 32.0f * 30.0f, 0};
            camera.yaw = 0.6f;
            camera.pitch = -0.3f;
        } else {
            camera.position = session.spawn;
            camera.position.y += game::kEyeHeight;
        }
    }

    // Benchmark mode: render frames into the framebuffer and report where the
    // time goes. It opens no window — what is being measured is the software
    // rasterizer, not the compositor — which also makes it usable anywhere.
    if (bench_frames > 0) {
        render::SceneRenderer bench_scene(kWidth, kHeight);
        const render::Vec3 bench_light = render::normalize(
            session.outdoor() ? render::Vec3{0.4f, 1.0f, 0.3f} : render::Vec3{0.3f, 1.0f, 0.2f});
        const render::Color bench_sky =
            session.outdoor() ? render::Color{135, 180, 220, 255} : render::Color{16, 16, 24, 255};

        std::vector<double> geometry;
        std::vector<double> billboards;
        geometry.reserve(static_cast<std::size_t>(bench_frames));
        billboards.reserve(static_cast<std::size_t>(bench_frames));

        for (int i = 0; i < bench_frames; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            bench_scene.begin(camera, bench_sky);
            if (session.outdoor()) {
                draw_outdoor(bench_scene, session, cache, bench_light);
            } else {
                draw_indoor(bench_scene, session, cache, bench_light);
            }
            const auto t1 = std::chrono::steady_clock::now();
            // Animation time advances with the frame so the billboard work is
            // not measured on one cached sprite.
            draw_billboards(bench_scene, session, cache, static_cast<std::uint32_t>(i));
            const auto t2 = std::chrono::steady_clock::now();

            using ms = std::chrono::duration<double, std::milli>;
            geometry.push_back(ms(t1 - t0).count());
            billboards.push_back(ms(t2 - t1).count());
        }

        auto report = [](const char* label, std::vector<double> v) {
            std::sort(v.begin(), v.end());
            const std::size_t p95 = (v.size() * 95) / 100;
            std::cout << "  " << label << ": median " << v[v.size() / 2] << " ms, p95 "
                      << v[p95 < v.size() ? p95 : v.size() - 1] << " ms\n";
        };
        std::vector<double> total(geometry.size());
        for (std::size_t i = 0; i < total.size(); ++i) {
            total[i] = geometry[i] + billboards[i];
        }
        std::cout << "bench " << bench_frames << " frames at " << kWidth << "x" << kHeight << "\n";
        std::cout << "  load: " << load_ms << " ms\n";
        report("geometry  ", geometry);
        report("billboards", billboards);
        report("frame     ", total);
        std::sort(total.begin(), total.end());
        std::cout << "  median fps: " << 1000.0 / total[total.size() / 2] << "\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "StarHaven - " + session.title() + " (" + session.file_name + ")";
    SDL_Window* window = SDL_CreateWindow(title.c_str(), kWidth, kHeight, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* screen = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ABGR8888,
                                            SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);

    const bool mouse_look = screenshot.empty();
    if (mouse_look) {
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    // A one-frame capture ends before a note sounds, so do not open audio
    // devices for it at all.
    game::MusicPlayer music;
    game::AmbientMixer ambient;
    if (screenshot.empty()) {
        if (const auto install = platform::install_from_env()) {
            if (music_wanted && session.music_track > 0 &&
                music.start(*install, session.music_track)) {
                std::cout << "playing track " << session.music_track << "\n";
            }
            if (!ambient_sources.empty() && ambient.open(*install)) {
                std::cout << ambient_sources.size() << " decorations make a sound\n";
            }
        }
    }

    // Outdoors the sun; indoors a fixed overhead lamp, since a level has no sky.
    const render::Vec3 light = render::normalize(
        session.outdoor() ? render::Vec3{0.4f, 1.0f, 0.3f} : render::Vec3{0.3f, 1.0f, 0.2f});
    const render::Color sky =
        session.outdoor() ? render::Color{135, 180, 220, 255} : render::Color{16, 16, 24, 255};

    const image::Font font = load_font(data_dir, "Lucida.fnt");

    // The tables the inspect panel reads. Both are already decoded; this is
    // the first thing that shows them.
    data::MonsterStatsTable monster_stats;
    {
        data::TextTable table;
        if (data::load_text_table(data_dir, "MONSTERS.TXT", table) == data::GameDataError::None) {
            (void)data::MonsterStatsTable::parse(table, monster_stats);
        }
    }
    data::ItemStatsTable item_stats;
    (void)data::load_item_stats(data_dir, item_stats);
    data::SpellStatsTable spell_stats;
    (void)data::load_spell_stats(data_dir, spell_stats);

    render::SceneRenderer scene(kWidth, kHeight);

    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    while (running) {
        ++frame;
        music.update();
        ambient.update(camera.position, ambient_sources, session.sounds);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB) {
                show_directory = !show_directory;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_look) {
                camera.yaw += event.motion.xrel * game::kMouseSensitivity;
                camera.pitch -= event.motion.yrel * game::kMouseSensitivity;
            }
        }

        const auto* keys = SDL_GetKeyboardState(nullptr);
        game::MoveInput in;
        in.forward = keys[SDL_SCANCODE_W];
        in.back = keys[SDL_SCANCODE_S];
        in.left = keys[SDL_SCANCODE_A];
        in.right = keys[SDL_SCANCODE_D];
        in.down = keys[SDL_SCANCODE_Q];
        in.up = keys[SDL_SCANCODE_E];
        in.speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;

        game::step_player(camera, fall_speed, fly, in, session.collision,
                          [&](float x, float z) { return session.terrain_height_at(x, z); });

        if (keys[SDL_SCANCODE_LEFT])
            camera.yaw -= game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_RIGHT])
            camera.yaw += game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_UP])
            camera.pitch += game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_DOWN])
            camera.pitch -= game::kLookSpeed * in.dt;
        camera.pitch =
            std::clamp(camera.pitch, -render::Camera::kMaxPitch, render::Camera::kMaxPitch);

        scene.begin(camera, sky);
        if (session.outdoor()) {
            draw_outdoor(scene, session, cache, light);
        } else {
            draw_indoor(scene, session, cache, light);
        }
        draw_billboards(scene, session, cache, game::sprite_ticks(SDL_GetTicks()));
        if (show_boxes && session.outdoor()) {
            draw_boxes(scene, session);
        }

        if (show_labels) {
            draw_labels(scene, session, font, camera.position);
        }
        if (show_directory) {
            draw_directory(scene, font, session);
        }
        // A thing behind a wall is not being looked at, whatever the aim says.
        auto visible = [&](const render::Vec3& at) {
            render::ScreenVertex p;
            if (!scene.project_point(at, p)) {
                return false;
            }
            return scene.framebuffer().depth_at(static_cast<int>(p.x), static_cast<int>(p.y)) >=
                   p.z - 0.0005f;
        };
        draw_panel(scene, font,
                   game::inspect(session, monster_stats, item_stats, spell_stats, camera.position,
                                 camera.forward(), visible));

        // The map's name, drawn with the game's own font.
        if (font.glyph_count() > 0) {
            game::draw_text(scene.framebuffer(), font, 8, 6, session.title(),
                            render::Color{255, 236, 170, 255}, render::Color{0, 0, 0, 255});
        }

        SDL_UpdateTexture(screen, nullptr, scene.framebuffer().color().data(), kWidth * 4);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, screen, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);

        if (!screenshot.empty() && frame >= game::kSettleFrames) {
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
