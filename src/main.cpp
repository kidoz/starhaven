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
#include "game/clock.hpp"
#include "game/combat.hpp"
#include "game/inspect.hpp"
#include "game/inventory.hpp"
#include "game/monster_ai.hpp"
#include "game/music_player.hpp"
#include "game/party.hpp"
#include "game/player.hpp"
#include "game/rest.hpp"
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
              << "  C          the character sheet; 1-4 choose a character\n"
              << "  I          the inventory; walk over a thing to pick it up\n"
              << "  Space      strike whatever you are aiming at, in reach\n"
              << "  R          rest, if nothing is close enough to object\n"
              << "  ESC/close  quit\n"
              << "\n"
              << "  --maps              list the maps and exit\n"
              << "  --pos X,Y,Z         start position (renderer axes, Y up)\n"
              << "  --look YAW,PITCH    start orientation in degrees\n"
              << "  --screenshot FILE   render one frame to a PPM and exit\n"
              << "  --bench N           render N frames, report timings, and exit\n"
              << "  --boxes             overlay model bounding boxes\n"
              << "  --labels            name the monsters and loot in the world\n"
              << "  --sheet N           open character N's sheet (1-4)\n"
              << "  --pack N            open character N's inventory (1-4)\n"
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
// `shown` is the animation each actor is currently in, which the fight
// changes: a monster flinches when hit and keeps its death picture after.
void draw_billboards(render::SceneRenderer& scene, const world::MapSession& session,
                     assets::AssetCache& cache, std::uint32_t ticks, const game::Mob& mob,
                     const render::Vec3& eye, const std::vector<std::string>& shown) {
    auto draw = [&](const std::string& animation, const render::Vec3& position, float scale,
                    game::SpriteView view = {}) {
        const game::SpriteChoice pick =
            game::choose_sprite(session.sprite_frames, animation, ticks, view.index);
        if (pick.entry.empty()) {
            return;
        }
        const render::Texture& tex = cache.sprite(pick.entry, pick.palette);
        if (tex.empty()) {
            return;
        }
        const float size = scale * pick.scale;
        scene.draw_billboard(position, static_cast<float>(tex.width()) * size,
                             static_cast<float>(tex.height()) * size, tex, 1.0f, view.mirror);
    };

    for (const auto& d : session.decorations) {
        draw(d.name, d.position, kDecorationScale);
    }
    // A monster is drawn from the side you are standing on: the view is the
    // angle between where it faces and where you are.
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        const auto& a = session.actors[i];
        const float to_eye = std::atan2(eye.z - a.position.z, eye.x - a.position.x);
        draw(shown[i].empty() ? a.animation : shown[i], a.position, kActorScale,
             game::sprite_view(mob.facing(i), to_eye));
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

// Draw a decoded bitmap straight into the framebuffer, one pixel to one
// pixel. The portraits are 59x79 and belong on the panel at their own size.
void blit(render::Framebuffer& fb, const render::Texture& texture, int left, int top) {
    if (texture.empty()) {
        return;
    }
    auto pixels = fb.color();
    const auto source = texture.pixels();
    for (int y = 0; y < static_cast<int>(texture.height()); ++y) {
        const int dy = top + y;
        if (dy < 0 || dy >= fb.height()) {
            continue;
        }
        for (int x = 0; x < static_cast<int>(texture.width()); ++x) {
            const int dx = left + x;
            if (dx < 0 || dx >= fb.width()) {
                continue;
            }
            const auto si =
                (static_cast<std::size_t>(y) * texture.width() + static_cast<std::size_t>(x)) * 4;
            if (source[si + 3] == 0) {
                continue;
            }
            const auto di =
                (static_cast<std::size_t>(dy) * fb.width() + static_cast<std::size_t>(dx)) * 4;
            pixels[di] = source[si];
            pixels[di + 1] = source[si + 1];
            pixels[di + 2] = source[si + 2];
        }
    }
}

// The character sheet: one member at a time, with the fields named the way the
// design tables name them.
void draw_sheet(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
                const game::Character& who, const data::DescriptionTable& stats,
                const data::DescriptionTable& classes) {
    if (font.glyph_count() == 0) {
        return;
    }
    auto pixels = scene.framebuffer().color();
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            pixels[i] = static_cast<std::uint8_t>(pixels[i] / 5);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 5);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 5);
        }
    }

    const render::Color white{230, 230, 230, 255};
    const render::Color dim{170, 170, 170, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;

    blit(scene.framebuffer(), cache.icon(game::portrait_entry(who.face)), 24, 28);
    game::draw_text(scene.framebuffer(), font, 100, 30, who.name, white, shadow);
    game::draw_text(scene.framebuffer(), font, 100, 30 + line,
                    who.class_name + ", level " + std::to_string(who.level), dim, shadow);

    // The seven attributes, named and ordered by stats.txt itself.
    int y = 120;
    for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
        const std::string_view label = game::stat_label(stats, a);
        const int value = who.attributes[a];
        const int bonus = game::attribute_bonus(value);
        std::string text = std::string(label) + "  " + std::to_string(value);
        text += bonus == 0
                    ? ""
                    : (bonus > 0 ? "  +" + std::to_string(bonus) : "  " + std::to_string(bonus));
        game::draw_text(scene.framebuffer(), font, 24, y, text, white, shadow);
        y += line;
    }

    // And the derived numbers, in the same order the table lists them.
    y = 120;
    const std::array<std::pair<std::size_t, std::string>, 5> derived{{
        {7, std::to_string(who.hit_points) + " / " + std::to_string(who.max_hit_points)},
        {8, std::to_string(who.armor_class)},
        {9, std::to_string(who.spell_points) + " / " + std::to_string(who.max_spell_points)},
        {12, std::to_string(who.age)},
        {14, std::to_string(who.experience)},
    }};
    for (const auto& [row, value] : derived) {
        game::draw_text(scene.framebuffer(), font, 260, y,
                        std::string(game::stat_label(stats, row)) + "  " + value, white, shadow);
        y += line;
    }

    // What the class is, in the designers' own words.
    if (const auto* described = classes.find(who.class_name);
        described != nullptr && !described->text.empty()) {
        const std::string text = data::cp1252_to_utf8(described->text.front());
        int x = 24;
        int wrap_y = y + line * 2;
        std::string word;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : ' ';
            if (ch != ' ' && ch != '\n') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > kWidth - 24) {
                x = 24;
                wrap_y += line;
            }
            if (wrap_y < kHeight - line) {
                game::draw_text(scene.framebuffer(), font, x, wrap_y, word, dim, shadow);
            }
            x += width;
            word.clear();
        }
    }

    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 6,
                    "1-4 choose a character, C closes", dim, shadow);
}

// Who is in the party, along the bottom-left: enough to know they exist and
// that the sheet keys mean something. The inspect panel sits bottom-right.
void draw_party_strip(render::SceneRenderer& scene, const image::Font& font,
                      const std::array<game::Character, 4>& party) {
    if (font.glyph_count() == 0) {
        return;
    }
    const int line = font.height() + 2;
    int y = kHeight - line * static_cast<int>(party.size()) - 8;
    for (std::size_t i = 0; i < party.size(); ++i) {
        const auto& who = party[i];
        std::string text = std::to_string(i + 1) + " " + who.name + "  " +
                           std::to_string(who.hit_points) + "/" +
                           std::to_string(who.max_hit_points);
        if (who.hit_points <= 0) {
            text += "  down";
        } else if (who.max_spell_points > 0) {
            text += "  sp " + std::to_string(who.spell_points) + "/" +
                    std::to_string(who.max_spell_points);
        }
        const render::Color colour = who.hit_points <= 0 ? render::Color{170, 110, 110, 255}
                                                         : render::Color{215, 215, 215, 255};
        game::draw_text(scene.framebuffer(), font, 8, y, text, colour, render::Color{0, 0, 0, 255});
        y += line;
    }
}

// One character's pack, on the grid, with the item art the game draws.
void draw_pack(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
               const game::Character& who, const game::Pack& pack,
               const data::ItemStatsTable& items) {
    if (font.glyph_count() == 0) {
        return;
    }
    auto pixels = scene.framebuffer().color();
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            pixels[i] = static_cast<std::uint8_t>(pixels[i] / 6);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 6);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 6);
        }
    }

    const render::Color white{230, 230, 230, 255};
    const render::Color dim{160, 160, 160, 255};
    const render::Color shadow{0, 0, 0, 255};
    constexpr int kLeft = 40;
    constexpr int kTop = 60;

    game::draw_text(scene.framebuffer(), font, kLeft, 24,
                    who.name + " is carrying " + std::to_string(pack.size()) +
                        (pack.size() == 1 ? " thing" : " things"),
                    white, shadow);

    // The empty grid first, so a pack with nothing in it still reads as a pack.
    for (int y = 0; y <= game::kPackHeight; ++y) {
        const int py = kTop + y * game::kCellSize;
        for (int x = 0; x < game::kPackWidth * game::kCellSize; ++x) {
            const int px = kLeft + x;
            if (px < 0 || px >= kWidth || py < 0 || py >= kHeight) {
                continue;
            }
            const auto i =
                (static_cast<std::size_t>(py) * kWidth + static_cast<std::size_t>(px)) * 4;
            pixels[i] = 70;
            pixels[i + 1] = 70;
            pixels[i + 2] = 60;
        }
    }

    for (const auto& carried : pack.items()) {
        const auto* row = items.at(static_cast<std::size_t>(carried.item_id));
        if (row == nullptr) {
            continue;
        }
        blit(scene.framebuffer(), cache.icon(row->picture), kLeft + carried.x * game::kCellSize,
             kTop + carried.y * game::kCellSize);
    }

    // And the list, since the art alone does not say what anything is worth.
    int y = kTop + (game::kPackHeight + 1) * game::kCellSize + 6;
    for (const auto& carried : pack.items()) {
        const auto* row = items.at(static_cast<std::size_t>(carried.item_id));
        if (row == nullptr || y > kHeight - font.height() - 20) {
            continue;
        }
        game::draw_text(scene.framebuffer(), font, kLeft, y,
                        data::cp1252_to_utf8(row->name) + "  " + std::to_string(row->value) +
                            " gold",
                        dim, shadow);
        y += font.height() + 1;
    }

    game::draw_text(scene.framebuffer(), font, kLeft, kHeight - font.height() - 8,
                    "1-4 choose a character, I closes", dim, shadow);
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
        std::string line = b.name + "  (" + b.type + ", " + std::to_string(b.opens) + "-" +
                           std::to_string(b.closes) + ")";
        if (!b.occupants.empty()) {
            line += "  \x97 " + b.occupants.front();
            if (b.occupants.size() > 1) {
                line += " +" + std::to_string(b.occupants.size() - 1);
            }
        }
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
    int open_sheet = 0;  // 1-4 to start with that character's sheet open
    int open_pack = 0;   // and the same for the inventory
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
        } else if (a == "--sheet" && i + 1 < argc) {
            open_sheet = std::atoi(argv[++i]);
        } else if (a == "--pack" && i + 1 < argc) {
            open_pack = std::atoi(argv[++i]);
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
    if (!session.monster_spawns.empty()) {
        std::cout << ", " << session.monster_spawns.size() << " spawn points";
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

    // The tables the inspect panel reads. Both are already decoded; this is
    // the first thing that shows them.
    data::MonsterStatsTable monster_stats;
    {
        data::TextTable table;
        if (data::load_text_table(data_dir, "MONSTERS.TXT", table) == data::GameDataError::None) {
            (void)data::MonsterStatsTable::parse(table, monster_stats);
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

        // The monsters stand still through a benchmark, but they are drawn
        // from whichever side they face, as they are in the game.
        game::Mob bench_mob;
        bench_mob.reset(session, monster_stats,
                        static_cast<std::uint32_t>(session.actors.size()) + 1u);
        const std::vector<std::string> bench_shown(session.actors.size());

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
            draw_billboards(bench_scene, session, cache, static_cast<std::uint32_t>(i), bench_mob,
                            camera.position, bench_shown);
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

    data::ItemStatsTable item_stats;
    (void)data::load_item_stats(data_dir, item_stats);
    data::SpellStatsTable spell_stats;
    (void)data::load_spell_stats(data_dir, spell_stats);

    // The party. Its names come from the game's own list; its numbers do not
    // come from anywhere, because no shipped table holds them. See
    // src/game/party.hpp.
    data::NameTable given_names;
    (void)data::load_names(data_dir, given_names);
    data::DescriptionTable stat_descriptions;
    data::DescriptionTable class_descriptions;
    (void)data::load_descriptions(data_dir, "stats.txt", stat_descriptions);
    (void)data::load_descriptions(data_dir, "Class.txt", class_descriptions);
    std::array<game::Character, 4> party = game::make_party(given_names, 1);
    std::array<game::Pack, 4> packs;
    int shown_member = open_sheet >= 1 && open_sheet <= 4 ? open_sheet - 1 : -1;
    int shown_pack = open_pack >= 1 && open_pack <= 4 ? open_pack - 1 : -1;
    std::string pick_up_message;
    std::uint64_t pick_up_shown = 0;

    // The monsters start where the map put them and wander from there.
    game::Mob mob;
    mob.reset(session, monster_stats, static_cast<std::uint32_t>(session.actors.size()) + 1u);
    game::Battle battle;
    battle.reset(session, monster_stats, static_cast<std::uint32_t>(session.actors.size()) + 7u);
    float party_recovery = 0.0f;
    int striker = 0;  // whose turn it is to swing

    // Time, and when this map is next due to refill. The interval is the map's
    // own: see docs/formats/text-tables.md.
    game::GameClock clock;
    std::int64_t next_refill = session.refill_days > 0 ? clock.day() + session.refill_days
                                                       : std::numeric_limits<std::int64_t>::max();

    // The animation each monster is drawn in, re-resolved only when the fight
    // changes it: looking a name up through the frame table every frame for
    // every monster is work nothing asked for.
    std::vector<world::MonsterAnimation> shown_kind(session.actors.size(),
                                                    world::MonsterAnimation::Stand);
    std::vector<std::string> shown_animation(session.actors.size());

    render::SceneRenderer scene(kWidth, kHeight);

    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    while (running) {
        ++frame;
        music.update();
        ambient.update(camera.position, ambient_sources, session.sounds);

        bool want_strike = false;
        bool want_rest = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB) {
                show_directory = !show_directory;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_C) {
                shown_member = shown_member < 0 ? 0 : -1;
                shown_pack = -1;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_I) {
                shown_pack = shown_pack < 0 ? 0 : -1;
                shown_member = -1;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key >= SDLK_1 &&
                       event.key.key <= SDLK_4) {
                const int chosen = static_cast<int>(event.key.key - SDLK_1);
                if (shown_member >= 0) {
                    shown_member = chosen;
                } else if (shown_pack >= 0) {
                    shown_pack = chosen;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE &&
                       shown_member < 0 && shown_pack < 0) {
                want_strike = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R &&
                       shown_member < 0 && shown_pack < 0) {
                want_rest = true;
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
        mob.update(in.dt, session, camera.position,
                   [&](std::size_t actor) { return battle.alive(actor); });
        if (std::string blow = battle.update(in.dt, session, monster_stats, party, camera.position);
            !blow.empty()) {
            pick_up_message = std::move(blow);
            pick_up_shown = SDL_GetTicks();
        }
        party_recovery = std::max(0.0f, party_recovery - in.dt);
        clock.advance_seconds(in.dt);
        battle.award(party);

        // The map refills on its own interval, whether the party slept
        // through it or walked through it.
        if (clock.day() >= next_refill) {
            battle.refill();
            next_refill = clock.day() + session.refill_days;
            pick_up_message = session.title() + " has filled with monsters again";
            pick_up_shown = SDL_GetTicks();
        }
        for (std::size_t i = 0; i < shown_kind.size(); ++i) {
            if (const world::MonsterAnimation kind = battle.animation_of(i);
                kind != shown_kind[i]) {
                shown_kind[i] = kind;
                shown_animation[i] =
                    game::actor_animation(session.monsters, session.sprite_frames, cache,
                                          session.actors[i].monster_id, kind);
            }
        }

        if (want_rest) {
            const bool disturbed =
                battle.anything_near(session, camera.position, game::kRestDisturbance);
            const game::RestResult result = game::rest(party, clock, disturbed);
            pick_up_message = game::rest_message(result, clock);
            pick_up_shown = SDL_GetTicks();
        }

        // A blow lands on whatever the party is aiming at, in reach, alive.
        if (want_strike && party_recovery <= 0.0f) {
            const render::Vec3 forward = camera.forward();
            if (const std::size_t target =
                    game::aimed_actor(session, battle, camera.position, forward, game::kPartyReach);
                target != game::kNoActor) {
                for (int tries = 0; tries < 4; ++tries) {
                    const auto who = static_cast<std::size_t>(striker);
                    striker = (striker + 1) % static_cast<int>(party.size());
                    if (party[who].hit_points <= 0) {
                        continue;
                    }
                    std::string blow = battle.strike(target, party[who], packs[who], session,
                                                     monster_stats, item_stats);
                    if (!blow.empty()) {
                        pick_up_message = std::move(blow);
                        pick_up_shown = SDL_GetTicks();
                        party_recovery = game::kPartyRecovery;
                    }
                    break;
                }
            }
        }
        if (const auto taken =
                game::take_nearby(session, item_stats, cache, camera.position, packs);
            !taken.empty()) {
            pick_up_message = taken;
            pick_up_shown = SDL_GetTicks();
        }
        draw_billboards(scene, session, cache, game::sprite_ticks(SDL_GetTicks()), mob,
                        camera.position, shown_animation);
        if (show_boxes && session.outdoor()) {
            draw_boxes(scene, session);
        }

        if (show_labels) {
            draw_labels(scene, session, font, camera.position);
        }
        if (show_directory) {
            draw_directory(scene, font, session);
        }
        if (shown_member < 0 && shown_pack < 0) {
            draw_party_strip(scene, font, party);
            game::draw_text(scene.framebuffer(), font, kWidth - font.text_width(clock.text()) - 8,
                            8, clock.text(), render::Color{210, 205, 185, 255},
                            render::Color{0, 0, 0, 255});
            if (!pick_up_message.empty() && SDL_GetTicks() - pick_up_shown < 3000) {
                game::draw_text(scene.framebuffer(), font, 8, 8, pick_up_message,
                                render::Color{235, 225, 170, 255}, render::Color{0, 0, 0, 255});
            }
        }
        if (shown_member >= 0) {
            draw_sheet(scene, font, cache, party[static_cast<std::size_t>(shown_member)],
                       stat_descriptions, class_descriptions);
        }
        if (shown_pack >= 0) {
            const auto who = static_cast<std::size_t>(shown_pack);
            draw_pack(scene, font, cache, party[who], packs[who], item_stats);
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
