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
#include <fstream>
#include <iostream>
#include <set>
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
#include "core/world/monster_spawn.hpp"
#include "game/ambient_mixer.hpp"
#include "game/clock.hpp"
#include "game/combat.hpp"
#include "game/conversation.hpp"
#include "game/daylight.hpp"
#include "game/enchant.hpp"
#include "game/hire.hpp"
#include "game/inspect.hpp"
#include "game/inventory.hpp"
#include "game/launches.hpp"
#include "game/monster_ai.hpp"
#include "game/music_player.hpp"
#include "game/party.hpp"
#include "game/promotion.hpp"
#include "game/player.hpp"
#include "game/rest.hpp"
#include "game/save.hpp"
#include "game/script_walk.hpp"
#include "game/shop.hpp"
#include "game/skills.hpp"
#include "game/sprites.hpp"
#include "game/temple.hpp"
#include "game/text.hpp"
#include "game/training.hpp"
#include "game/traps.hpp"
#include "game/travel.hpp"

namespace {

using namespace starhaven;

// Where a save lands: beside where the engine was run, in this
// engine's own text format.
constexpr const char* kSaveFile = "starhaven.save";  // slot 1's old name, still read

// Nine numbered slots; slot 1 keeps the old single file's name so existing
// saves survive the change.
[[nodiscard]] std::string save_slot_path(int slot) {
    return slot <= 1 ? kSaveFile : "starhaven-" + std::to_string(slot) + ".save";
}

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
              << "  U          in a pack: drink a potion, or learn from a book\n"
              << "  M          in a pack: pour the first potion into the second\n"
              << "  X          read the first spell scroll at what you aim at\n"
              << "  H          cast a known spell: heal the wounded, or smite the aimed\n"
              << "  Space      strike whatever you are aiming at, in reach\n"
              << "  R          rest, if nothing is close enough to object\n"
              << "  F5/F9      save / load the current slot; F6 turns to the next\n"
              << "  Tab then 1-9  trade with an establishment on this map\n"
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
              << "  --time HOUR         start at this hour of day (0-23)\n"
              << "  --create            shape the party before the world starts\n"
              << "  --shop N            open the Nth establishment's counter\n"
              << "  --fly               disable gravity and collision\n"
              << "  --walk N            use map event N on startup (research)\n"
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
                  assets::AssetCache& cache, const render::Vec3& sun, float level = 1.0f) {
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
        const float lambert = (std::clamp(render::dot(n, sun), 0.0f, 1.0f) * 0.8f + 0.2f) * level;
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
                (std::clamp(std::abs(render::dot(n, sun)), 0.0f, 1.0f) * 0.8f + 0.2f) * level;
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

template <typename BoundsFn>
void draw_indoor(render::SceneRenderer& scene, const world::MapSession& session,
                 assets::AssetCache& cache, const render::Vec3& lamp, float glow,
                 const std::vector<float>* baked, BoundsFn skippable) {
    for (std::size_t index = 0; index < session.blv.faces.size(); ++index) {
        const auto& f = session.blv.faces[index];
        if (f.invisible() || f.vertex_count < 3) {
            continue;
        }
        if (skippable(index)) {
            continue;
        }
        const render::Vec3 n = render::normalize(render::Vec3{f.nx(), f.nz(), f.ny()});
        // With the map's own lights baked, a face is lit by them over a dim
        // floor; without, the old uniform lamp carries the whole level.
        float level = std::clamp(std::abs(render::dot(n, lamp)), 0.0f, 1.0f) * 0.7f + 0.3f;
        if (baked != nullptr && index < baked->size()) {
            level = (std::clamp(std::abs(render::dot(n, lamp)), 0.0f, 1.0f) * 0.25f + 0.3f) +
                    (*baked)[index] * 0.65f;
        }
        const float lambert = std::clamp(level * glow, 0.0f, 1.0f);

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
                     const render::Vec3& eye, const std::vector<std::string>& shown,
                     const std::vector<game::ActiveLaunch>& launches = {}) {
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
    // What a script launched, mid-flight.
    for (const auto& l : launches) {
        draw(l.animation, l.position, kObjectScale);
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

// The bottom `frac` of a vertical gauge, bottom-anchored the way a
// draining column reads.
void blit_gauge(render::Framebuffer& fb, const render::Texture& texture, int left, int top,
                float frac) {
    if (texture.empty() || frac <= 0.0f) {
        return;
    }
    const int height = static_cast<int>(texture.height());
    const int skip = height - std::min(height, static_cast<int>(frac * static_cast<float>(height)));
    auto pixels = fb.color();
    const auto source = texture.pixels();
    for (int y = skip; y < height; ++y) {
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
            const auto di =
                (static_cast<std::size_t>(dy) * fb.width() + static_cast<std::size_t>(dx)) * 4;
            pixels[di] = source[si];
            pixels[di + 1] = source[si + 1];
            pixels[di + 2] = source[si + 2];
        }
    }
}

// The game's own screen furniture, placed by the pieces' own sizes: BORDER3
// (468x8) tops the view and BORDER4 (8x344) flanks it, which pins the 3D
// viewport at (8,8) 460x344 — the game's own; Border1.pcx (172x339) is the
// right column, Border2.pcx (469x109) the portrait bar and FOOTER (483x24)
// the message strip, each abutting the last exactly. The corner right of
// the portraits no shipped piece claims; it is filled dark and the engine's
// own readouts live there. `inferred` for the fit, the pieces' sizes are
// `observed`.
void draw_frame(render::SceneRenderer& scene, assets::AssetCache& cache) {
    blit(scene.framebuffer(), cache.icon("BORDER3"), 0, 0);
    blit(scene.framebuffer(), cache.icon("BORDER4"), 0, 8);
    blit(scene.framebuffer(), cache.icon("Border1.pcx"), 468, 0);
    blit(scene.framebuffer(), cache.icon("Border2.pcx"), 0, 352);
    blit(scene.framebuffer(), cache.icon("FOOTER"), 0, kHeight - 24);
    auto pixels = scene.framebuffer().color();
    const auto fill = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i =
                    (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
                pixels[i] = 24;
                pixels[i + 1] = 22;
                pixels[i + 2] = 20;
            }
        }
    };
    fill(468, 339, kWidth, kHeight - 24);
    fill(483, kHeight - 24, kWidth, kHeight);
}

// The character sheet: one member at a time, with the fields named the way the
// design tables name them.
void draw_sheet(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
                const game::Character& who, const data::DescriptionTable& stats,
                const data::DescriptionTable& classes, std::int64_t minute,
                const data::JournalTable& awards, const std::set<int>& earned) {
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

    blit(scene.framebuffer(), cache.icon(game::portrait_entry(
                                  who.face, game::portrait_frame_of(who, false))),
         24, 28);
    game::draw_text(scene.framebuffer(), font, 100, 30, who.name, white, shadow);
    game::draw_text(scene.framebuffer(), font, 100, 30 + line,
                    who.class_name + ", level " + std::to_string(who.level), dim, shadow);

    // The seven attributes, named and ordered by stats.txt itself.
    int y = 120;
    for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
        const std::string_view label = game::stat_label(stats, a);
        const int value = who.attribute(static_cast<game::Attribute>(a));
        const int bonus = game::attribute_bonus(value);
        std::string text = std::string(label) + "  " + std::to_string(value);
        if (who.temp_attributes[a] > 0) {
            text += " (of it +" + std::to_string(who.temp_attributes[a]) + " temporary)";
        }
        text += bonus == 0
                    ? ""
                    : (bonus > 0 ? "  +" + std::to_string(bonus) : "  " + std::to_string(bonus));
        game::draw_text(scene.framebuffer(), font, 24, y, text, white, shadow);
        y += line;
    }

    // The named conditions the potions set, with the sheet's own hours.
    {
        std::string active;
        const auto note = [&](const char* name, std::int64_t until) {
            if (until > minute) {
                active += (active.empty() ? "" : ", ");
                active += name;
            }
        };
        note("hasted", who.haste_until);
        note("blessed", who.bless_until);
        note("heroic", who.heroism_until);
        note("stone skin", who.stone_skin_until);
        if (who.temp_armor > 0) {
            active += (active.empty() ? "" : ", ");
            active += "+" + std::to_string(who.temp_armor) + " AC until rest";
        }
        if (!active.empty()) {
            game::draw_text(scene.framebuffer(), font, 24, y + line, active,
                            render::Color{170, 215, 170, 255}, shadow);
        }
    }

    // The skills held, each with what raising it would cost. The staircase
    // is the engine's own; the effects are the table's lines.
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y,
                    "Skills  (" + std::to_string(who.skill_points) + " to spend)", white,
                    shadow);
    y += line;
    int numbered = 5;
    for (const auto& [skill, points] : who.skills) {
        std::string label = std::to_string(numbered) + "  " + skill + "  " +
                            std::to_string(points) + "  (raise for " +
                            std::to_string(game::raise_cost(points)) + ")";
        game::draw_text(scene.framebuffer(), font, 24, y, label,
                        who.skill_points >= game::raise_cost(points) ? white : dim, shadow);
        y += line;
        if (++numbered > 9) {
            break;
        }
    }

    // And the derived numbers, in the same order the table lists them.
    const int left_bottom = y;
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

    // The honors the quests set, worded by Awards.txt itself.
    if (!earned.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 260, y, "Honors", white, shadow);
        y += line;
        std::size_t listed = 0;
        for (const auto& row : awards.entries()) {
            if (!earned.contains(row.bit) || !row.has_text()) {
                continue;
            }
            if (++listed > 4) {
                game::draw_text(scene.framebuffer(), font, 268, y, "...", dim, shadow);
                break;
            }
            game::draw_text(scene.framebuffer(), font, 268, y, data::cp1252_to_utf8(row.text),
                            dim, shadow);
            y += line;
        }
    }

    // What the class is, in the designers' own words, below both columns.
    if (const auto* described = classes.find(who.class_name);
        described != nullptr && !described->text.empty()) {
        const std::string text = data::cp1252_to_utf8(described->text.front());
        int x = 24;
        int wrap_y = std::max(left_bottom, y) + line * 2;
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
            if (wrap_y < kHeight - line * 2 - 6) {
                game::draw_text(scene.framebuffer(), font, x, wrap_y, word, dim, shadow);
            }
            x += width;
            word.clear();
        }
    }

    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 6,
                    "1-4 choose a character, 5-9 raise a skill, C closes", dim, shadow);
}

// Who is in the party, seated in the portrait bar's own ovals: Border2's
// dark seats sit 113 pixels apart starting at x=22, with a narrow gauge
// groove either side of each — both measured from the art itself. The face
// wears the frame its condition picks from the portrait sheet's own 53.
// Hits fill the left groove and mana the right; which is which the art
// does not say. `inferred`
void draw_party_strip(render::SceneRenderer& scene, assets::AssetCache& cache,
                      const std::array<game::Character, 4>& party,
                      const std::array<bool, 4>& wincing) {
    for (std::size_t i = 0; i < party.size(); ++i) {
        const auto& who = party[i];
        const int left = 22 + static_cast<int>(i) * 113;
        const int frame = game::portrait_frame_of(who, wincing[i]);
        blit(scene.framebuffer(), cache.icon(game::portrait_entry(who.face, frame)), left, 361);
        if (who.max_hit_points > 0) {
            const float hits = std::clamp(
                static_cast<float>(who.hit_points) / static_cast<float>(who.max_hit_points),
                0.0f, 1.0f);
            blit_gauge(scene.framebuffer(), cache.icon("HITSFULL"), left - 8, 361, hits);
        }
        if (who.max_spell_points > 0) {
            const float mana = std::clamp(
                static_cast<float>(who.spell_points) / static_cast<float>(who.max_spell_points),
                0.0f, 1.0f);
            blit_gauge(scene.framebuffer(), cache.icon("MANAFULL"), left + 65, 361, mana);
        }
    }
}

// Shaping the party before the world starts: the twelve portraits, the six
// base classes and the names are the game's own; the numbers are rolled by
// this engine and say so on the sheet.
void draw_creation(render::SceneRenderer& scene, const image::Font& font,
                   assets::AssetCache& cache, const std::array<game::Character, 4>& party,
                   int slot, const data::DescriptionTable& stats,
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
    const render::Color mark{235, 225, 170, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;

    game::draw_text(scene.framebuffer(), font, 24, 8, "Who goes to Enroth?", white, shadow);
    for (int i = 0; i < 4; ++i) {
        const auto& who = party[static_cast<std::size_t>(i)];
        const int x = 40 + i * 150;
        blit(scene.framebuffer(), cache.icon(game::portrait_entry(who.face)), x, 32);
        game::draw_text(scene.framebuffer(), font, x, 116,
                        std::to_string(i + 1) + " " + who.name, i == slot ? mark : white,
                        shadow);
        game::draw_text(scene.framebuffer(), font, x, 116 + line, who.class_name,
                        i == slot ? mark : dim, shadow);
    }

    const auto& who = party[static_cast<std::size_t>(slot)];
    int y = 160;
    for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
        game::draw_text(scene.framebuffer(), font, 40, y,
                        std::string(game::stat_label(stats, a)) + "  " +
                            std::to_string(who.attribute(static_cast<game::Attribute>(a))),
                        white, shadow);
        y += line;
    }
    game::draw_text(scene.framebuffer(), font, 240, 160,
                    "Hit Points  " + std::to_string(who.max_hit_points), white, shadow);
    game::draw_text(scene.framebuffer(), font, 240, 160 + line,
                    "Spell Points  " + std::to_string(who.max_spell_points), white, shadow);
    game::draw_text(scene.framebuffer(), font, 240, 160 + line * 3,
                    "the rolls are this engine's own", dim, shadow);

    // The class's own description, wrapped.
    if (const auto* described = classes.find(who.class_name);
        described != nullptr && !described->text.empty()) {
        const std::string text = data::cp1252_to_utf8(described->text.front());
        std::string word;
        int x = 40;
        y = 160 + line * 9;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : ' ';
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > kWidth - 40) {
                x = 40;
                y += line;
            }
            if (y < kHeight - line * 3 - 8) {
                game::draw_text(scene.framebuffer(), font, x, y, word, dim, shadow);
            }
            x += width;
            word.clear();
        }
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-4 choose, C class, F face, N name, R reroll, Enter begins", dim, shadow);
}

// The journal: what the held quest bits say, in Quests.txt's own words,
// and the honors under them. The walker has kept this state all along;
// this is the first page it is written on.
void draw_journal(render::SceneRenderer& scene, const image::Font& font,
                  const data::JournalTable& quests, const data::JournalTable& awards,
                  const std::set<int>& bits, const std::set<int>& earned) {
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
    const render::Color dim{170, 170, 170, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 10;
    game::draw_text(scene.framebuffer(), font, 24, y, "Journal", white, shadow);
    y += line * 2;

    const auto wrap = [&](const std::string& text, const render::Color& colour) {
        std::string word;
        int x = 24;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : ' ';
            if (ch != ' ' && ch != '\n') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > kWidth - 24) {
                x = 24;
                y += line;
            }
            if (y < kHeight - line * 2 - 8) {
                game::draw_text(scene.framebuffer(), font, x, y, word, colour, shadow);
            }
            x += width;
            word.clear();
        }
        y += line;
    };

    std::size_t noted = 0;
    for (const auto& row : quests.entries()) {
        if (!bits.contains(row.bit) || !row.has_text()) {
            continue;
        }
        ++noted;
        wrap("\x95 " + data::cp1252_to_utf8(row.text), white);
        y += 2;
        if (y >= kHeight - line * 3) {
            game::draw_text(scene.framebuffer(), font, 24, y, "...", dim, shadow);
            break;
        }
    }
    if (noted == 0) {
        game::draw_text(scene.framebuffer(), font, 24, y, "Nothing is asked of the party yet.",
                        dim, shadow);
        y += line * 2;
    }
    if (!earned.empty() && y < kHeight - line * 4) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, "Honors", white, shadow);
        y += line;
        for (const auto& row : awards.entries()) {
            if (!earned.contains(row.bit) || !row.has_text() || y >= kHeight - line * 2 - 8) {
                continue;
            }
            game::draw_text(scene.framebuffer(), font, 32, y, data::cp1252_to_utf8(row.text),
                            dim, shadow);
            y += line;
        }
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8, "J closes", dim, shadow);
}

// One character's pack, on the grid, with the item art the game draws.
void draw_pack(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
               const game::Character& who, const game::Pack& pack,  // NOLINT
               const data::ItemStatsTable& items, const data::StandardBonusTable& standard,
               const data::SpecialBonusTable& special, int cursor_x = -1, int cursor_y = -1,
               int sale_offer = -1) {
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

    // The cursor: a bright cell border, and what the thing under it is —
    // with what a counter would pay, when one is open.
    if (cursor_x >= 0 && cursor_y >= 0) {
        const int px = kLeft + cursor_x * game::kCellSize;
        const int py = kTop + cursor_y * game::kCellSize;
        for (int d = 0; d < game::kCellSize; ++d) {
            for (const auto [ex, ey] : {std::pair{px + d, py}, {px + d, py + game::kCellSize},
                                        {px, py + d}, {px + game::kCellSize, py + d}}) {
                if (ex >= 0 && ex < kWidth && ey >= 0 && ey < kHeight) {
                    const auto i =
                        (static_cast<std::size_t>(ey) * kWidth + static_cast<std::size_t>(ex)) *
                        4;
                    pixels[i] = 230;
                    pixels[i + 1] = 220;
                    pixels[i + 2] = 150;
                }
            }
        }
        if (const auto* under = pack.at(cursor_x, cursor_y); under != nullptr) {
            const auto* row = items.at(static_cast<std::size_t>(under->item_id));
            if (row != nullptr) {
                std::string line =
                    under->identified || row->unidentified_name.empty()
                        ? data::cp1252_to_utf8(row->name)
                        : data::cp1252_to_utf8(row->unidentified_name) + " (unidentified)";
                if (sale_offer >= 0) {
                    line += "   the counter pays " + std::to_string(sale_offer) +
                            " gold, S sells";
                }
                game::draw_text(scene.framebuffer(), font, kLeft + 200, 24, line, white, shadow);
            }
        }
    }

    // And the list, since the art alone does not say what anything is worth.
    int y = kTop + (game::kPackHeight + 1) * game::kCellSize + 6;
    for (const auto& carried : pack.items()) {
        const auto* row = items.at(static_cast<std::size_t>(carried.item_id));
        if (row == nullptr || y > kHeight - font.height() - 20) {
            continue;
        }
        // An unidentified thing shows its table's own unknown name and
        // keeps its worth to itself.
        const std::string label =
            carried.identified || row->unidentified_name.empty()
                ? data::cp1252_to_utf8(game::enchanted_name(
                      row->name, standard.at(static_cast<std::size_t>(carried.standard_bonus)),
                      special.at(static_cast<std::size_t>(carried.special_bonus)))) +
                      "  " + std::to_string(row->value) + " gold"
                : data::cp1252_to_utf8(row->unidentified_name) + "  (unidentified)";
        game::draw_text(scene.framebuffer(), font, kLeft, y, label, dim, shadow);
        y += font.height() + 1;
    }

    // The paperdoll: the panel, then the body whose letter is the face's —
    // the twelve dolls and the twelve portraits share their eight-male,
    // four-female lettering. An item's Equip X/Y is a point on the 640x480
    // screen, and the items themselves place the body: all seven boots'
    // art bottoms out at screen row 350, the body is 298 tall, and the helms
    // centre on column 561 — so the body stands at (504, 52), its feet on
    // the panel's bottom edge with the panel flush in the corner. `observed`
    // — see docs/formats/paperdoll.md.
    constexpr int kDollLeft = kWidth - 173;
    constexpr int kDollTop = 0;
    constexpr int kBodyLeft = 504;
    constexpr int kBodyTop = 52;
    blit(scene.framebuffer(), cache.icon("BACKDOLL"), kDollLeft, kDollTop);

    // What is worn, by its equip type: a cloak's larger half hangs behind
    // the body, body armor swaps the torso for its own overlay, and
    // everything else is the item's art at its recorded point.
    const auto worn = [&](data::ItemEquipType type) -> const data::ItemStatsEntry* {
        for (const int id : who.equipped) {
            if (id <= 0) {
                continue;
            }
            const auto* row = items.at(static_cast<std::size_t>(id));
            if (row != nullptr && row->equip_type == type) {
                return row;
            }
        }
        return nullptr;
    };
    if (const auto* cloak = worn(data::ItemEquipType::Cloak);
        cloak != nullptr && !cloak->picture.empty() && cloak->picture.back() == 'a') {
        std::string back = cloak->picture;
        back.back() = 'b';
        blit(scene.framebuffer(), cache.icon(back), kBodyLeft - 15, kBodyTop + 60);
    }

    const bool female = game::face_is_female(who.face);
    std::string body = female ? "grl" : "ml";
    body += static_cast<char>('a' + who.face - (female ? game::kMaleFaceCount : 0));
    blit(scene.framebuffer(), cache.icon(body + "bod"), kBodyLeft, kBodyTop);

    if (const auto* armor = worn(data::ItemEquipType::Armor);
        armor != nullptr && armor->picture.size() > 4) {
        // "chn1icon" wears as "chn1bod", centred on the body with its top at
        // the shoulder line. The rule is calibrated by eye. `inferred`
        const std::string stem = armor->picture.substr(0, armor->picture.size() - 4);
        const render::Texture& torso = cache.icon(stem + "bod");
        blit(scene.framebuffer(), torso,
             kBodyLeft + (114 - static_cast<int>(torso.width())) / 2, kBodyTop + 60);
    }

    for (const int id : who.equipped) {
        if (id <= 0) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(id));
        if (row != nullptr && (row->equip_x != 0 || row->equip_y != 0)) {
            blit(scene.framebuffer(), cache.icon(row->picture), row->equip_x, row->equip_y);
        }
    }

    // What this character is wearing, beside the grid.
    int worn_y = kTop;
    for (std::size_t i = 0; i < game::kSlotCount; ++i) {
        const int id = who.equipped[i];
        if (id <= 0) {
            continue;
        }
        const auto* row = items.at(static_cast<std::size_t>(id));
        if (row == nullptr) {
            continue;
        }
        game::draw_text(scene.framebuffer(), font, kLeft + game::kPackWidth * game::kCellSize + 12,
                        worn_y,
                        std::string(game::slot_name(static_cast<game::Slot>(i))) + ": " +
                            data::cp1252_to_utf8(row->name) +
                            (who.equipped_broken[i] ? " (broken)" : ""),
                        white, shadow);
        worn_y += font.height() + 1;
    }

    game::draw_text(scene.framebuffer(), font, kLeft, kHeight - font.height() - 8,
                    "arrows choose, E wears, U drinks, M mixes from the chosen cell, I closes", dim, shadow);
}

// A temple's counter: the two verbs its margin notes name, at its own Val.
void draw_temple(render::SceneRenderer& scene, const image::Font& font,
                 const data::BuildingStatsEntry& shop,
                 const std::array<game::Character, 4>& party, int gold,
                 const std::string& said) {
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
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 24;
    game::draw_text(scene.framebuffer(), font, 24, y,
                    data::cp1252_to_utf8(shop.name) + " \x97 " + shop.type + ", " +
                        data::cp1252_to_utf8(shop.proprietor),
                    white, shadow);
    y += line;
    const game::TempleService service = game::temple_service(shop);
    std::string terms = "you have " + std::to_string(gold) + " gold; healing costs " +
                        std::to_string(game::heal_price(shop));
    if (!service.heals_eradicated || !service.heals_dead || !service.heals_stone) {
        terms += "; this house cannot mend";
        if (!service.heals_dead) {
            terms += " the dead,";
        }
        if (!service.heals_stone) {
            terms += " the stoned,";
        }
        if (!service.heals_eradicated) {
            terms += " the eradicated";
        }
        if (terms.back() == ',') {
            terms.pop_back();
        }
    }
    game::draw_text(scene.framebuffer(), font, 24, y, terms, dim, shadow);
    y += line * 2;
    for (std::size_t i = 0; i < party.size(); ++i) {
        const auto& who = party[i];
        std::string text = std::to_string(i + 1) + "  " + who.name + "  " +
                           std::to_string(who.hit_points) + "/" +
                           std::to_string(who.max_hit_points);
        const bool needs = who.hit_points < who.max_hit_points ||
                           who.spell_points < who.max_spell_points || who.poisoned > 0;
        if (who.poisoned > 0) {
            text += "  poisoned";
        }
        if (who.hit_points <= 0) {
            text += "  down";
        }
        if (!needs) {
            text += "  whole";
        }
        game::draw_text(scene.framebuffer(), font, 24, y, text, needs ? white : dim, shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-4 heal a character, 5 donate, T talk, B closes", dim, shadow);
}

// A bank's counter: the balance, and the sheet's own two verbs.
void draw_bank(render::SceneRenderer& scene, const image::Font& font,
               const data::BuildingStatsEntry& shop, int gold, int balance,
               const std::string& said) {
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
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 24;
    game::draw_text(scene.framebuffer(), font, 24, y,
                    data::cp1252_to_utf8(shop.name) + " \x97 " + shop.type + ", " +
                        data::cp1252_to_utf8(shop.proprietor),
                    white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y,
                    "you carry " + std::to_string(gold) + " gold; the vault holds " +
                        std::to_string(balance),
                    dim, shadow);
    y += line * 2;
    game::draw_text(scene.framebuffer(), font, 24, y, "1  deposit 100", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y, "2  deposit all", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y, "3  withdraw 100", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y, "4  withdraw all", white, shadow);
    y += line;
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "T talk to whoever is here, B closes", dim, shadow);
}

// A training hall's counter: who can train, to what, and for how much.
void draw_training(render::SceneRenderer& scene, const image::Font& font,
                   const data::BuildingStatsEntry& shop,
                   const std::array<game::Character, 4>& party, int gold,
                   const std::string& said) {
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
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 24;

    game::draw_text(scene.framebuffer(), font, 24, y,
                    data::cp1252_to_utf8(shop.name) + " \x97 " + shop.type + ", " +
                        data::cp1252_to_utf8(shop.proprietor),
                    white, shadow);
    y += line;
    std::string ceiling = "you have " + std::to_string(gold) + " gold";
    if (const int top = game::max_level_of(shop); top > 0) {
        ceiling += "; this hall trains to level " + std::to_string(top);
    }
    game::draw_text(scene.framebuffer(), font, 24, y, ceiling, dim, shadow);
    y += line * 2;

    for (std::size_t i = 0; i < party.size(); ++i) {
        const auto& who = party[i];
        const game::TrainingOffer offer = game::training_offer(shop, who);
        std::string text = std::to_string(i + 1) + "  " + who.name + " \x97 level " +
                           std::to_string(who.level);
        bool ready = false;
        if (offer.to_level == 0) {
            text += ", beyond this hall";
        } else if (offer.experience_needed > 0) {
            text += ", needs " + std::to_string(offer.experience_needed) +
                    " more experience for level " + std::to_string(offer.to_level);
        } else {
            text += ", ready for level " + std::to_string(offer.to_level) + " \x97 " +
                    std::to_string(offer.cost) + " gold";
            ready = offer.cost <= gold;
        }
        game::draw_text(scene.framebuffer(), font, 24, y, text, ready ? white : dim, shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-4 train a character, T talk to whoever is here, B closes", dim, shadow);
}

// A travel counter: where the rides go, when they leave, and the fare.
void draw_travel(render::SceneRenderer& scene, const image::Font& font,
                 const data::BuildingStatsEntry& shop,
                 const std::vector<game::TravelRoute>& routes, int fare,
                 const game::GameClock& clock, int gold, const std::string& said) {
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
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 24;

    game::draw_text(scene.framebuffer(), font, 24, y,
                    data::cp1252_to_utf8(shop.name) + " \x97 " + shop.type + ", " +
                        data::cp1252_to_utf8(shop.proprietor),
                    white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y,
                    "you have " + std::to_string(gold) + " gold; today is " +
                        std::string(clock.weekday()),
                    dim, shadow);
    y += line * 2;

    static constexpr std::array<std::string_view, 7> kShortDays{"Su", "M", "Tu", "W",
                                                                "Th", "F", "Sa"};
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const auto& route = routes[i];
        std::string text = std::to_string(i + 1) + "  " + route.destination + "  \x97 leaves";
        for (std::size_t d = 0; d < kShortDays.size(); ++d) {
            if (route.leaves[d]) {
                text += " " + std::string(kShortDays[d]);
            }
        }
        text += ", " + std::to_string(route.days) +
                (route.days == 1 ? " day, " : " days, ") + std::to_string(fare) + " gold";
        if (route.leaves_on(clock.day())) {
            text += "  (leaves today)";
        }
        game::draw_text(scene.framebuffer(), font, 24, y, text,
                        route.leaves_on(clock.day()) && fare <= gold ? white : dim, shadow);
        y += line;
    }
    if (routes.empty()) {
        game::draw_text(scene.framebuffer(), font, 24, y, "No rides leave from here.", dim,
                        shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-3 ride, T talk to whoever is here, B closes", dim, shadow);
}

// A shop's counter: what it has, what it wants for it, and what the
// shopkeeper says about the state of your purse.
void draw_shop(render::SceneRenderer& scene, const image::Font& font,
               const data::BuildingStatsEntry& shop, const std::vector<game::StockItem>& stock,
               const data::ItemStatsTable& items, const data::MerchantTextTable& words, int gold,
               const std::string& said) {
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
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 24;

    game::draw_text(scene.framebuffer(), font, 24, y,
                    data::cp1252_to_utf8(shop.name) + " \x97 " + shop.type + ", " +
                        data::cp1252_to_utf8(shop.proprietor),
                    white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 24, y, "you have " + std::to_string(gold) + " gold",
                    dim, shadow);
    y += line * 2;

    const std::size_t shown = std::min<std::size_t>(stock.size(), 9);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto* row = items.at(static_cast<std::size_t>(stock[i].item_id));
        if (row == nullptr) {
            continue;
        }
        game::draw_text(scene.framebuffer(), font, 24, y,
                        std::to_string(i + 1) + "  " + data::cp1252_to_utf8(row->name) + "  " +
                            std::to_string(stock[i].price) + " gold",
                        stock[i].price <= gold ? white : dim, shadow);
        y += line;
    }
    if (stock.empty()) {
        game::draw_text(scene.framebuffer(), font, 24, y, "The shelves are bare.", dim, shadow);
        y += line;
    }

    // The shopkeeper's own words, from Merchant.txt.
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 24, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-9 buy, S sell, F repair, T talk, B closes", dim, shadow);
}

// Somebody in an establishment, and what they have to say.
void draw_conversation(render::SceneRenderer& scene, const image::Font& font,
                       const game::Conversation& talk, const std::string& answer) {
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
    const render::Color dim{170, 170, 170, 255};
    const render::Color said{235, 225, 170, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 28;

    game::draw_text(scene.framebuffer(), font, 24, y, talk.who, white, shadow);
    y += line * 2;
    if (!talk.greeting.empty()) {
        game::draw_text(scene.framebuffer(), font, 24, y, talk.greeting, said, shadow);
        y += line * 2;
    }
    if (!talk.today.empty()) {
        game::draw_text(scene.framebuffer(), font, 24, y, "today: " + talk.today, dim, shadow);
        y += line * 2;
    }
    for (std::size_t i = 0; i < talk.topics.size(); ++i) {
        game::draw_text(scene.framebuffer(), font, 24, y,
                        std::to_string(i + 1) + "  " + talk.topics[i], white, shadow);
        y += line;
    }
    if (!answer.empty()) {
        y += line;
        // The answers run long, so they wrap.
        std::string word;
        int x = 24;
        for (std::size_t i = 0; i <= answer.size(); ++i) {
            const char ch = i < answer.size() ? answer[i] : ' ';
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > kWidth - 24) {
                x = 24;
                y += line;
            }
            if (y < kHeight - line * 3) {
                game::draw_text(scene.framebuffer(), font, x, y, word, said, shadow);
            }
            x += width;
            word.clear();
        }
        y += line;
    }
    if (!talk.approaches.empty()) {
        std::string how = "you could ";
        for (std::size_t i = 0; i < talk.approaches.size(); ++i) {
            how += (i == 0 ? "" : i + 1 == talk.approaches.size() ? " or " : ", ");
            how += talk.approaches[i];
        }
        game::draw_text(scene.framebuffer(), font, 24, kHeight - line * 2 - 8,
                        how + " them (5 beg, 6 bribe, 7 threaten)", dim, shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-3 ask about something, H hires or dismisses, T closes", dim, shadow);
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
    const int x0 = kWidth - box_w - 4;
    const int y0 = kHeight - box_h - 26;

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
                    const world::MapSession& session, const game::GameClock& clock,
                    const data::ProfessionTextTable& trade_talk) {
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
        // Open or shut, against the hours 2DEvents.txt gives this one.
        std::string line = b.name + "  (" + b.type + ", " + std::to_string(b.opens) + "-" +
                           std::to_string(b.closes) +
                           (clock.open(b.opens, b.closes) ? ", open)" : ", shut)");
        if (!b.occupants.empty()) {
            line += "  \x97 " + b.occupants.front();
            if (b.occupants.size() > 1) {
                line += " +" + std::to_string(b.occupants.size() - 1);
            }
            // What that trade has to say on today's day of the week.
            const int profession =
                b.occupant_professions.empty() ? 0 : b.occupant_professions.front();
            if (const auto* said = trade_talk.at(profession); said != nullptr) {
                const std::size_t day = static_cast<std::size_t>(clock.day() % 7);
                if (!said->days[day].topic.empty()) {
                    line += "  on " + std::string(clock.weekday()) + ": " +
                            data::cp1252_to_utf8(said->days[day].topic);
                }
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
    int open_sheet = 0;   // 1-4 to start with that character's sheet open
    int open_pack = 0;    // and the same for the inventory
    int start_hour = -1;  // --time, for looking at the world at a given hour
    int start_shop = 0;   // --shop, to open a counter straight away
    int walk_on_start = -1;  // a map event to use on startup, for reproducing traps
    bool force_create = false;  // --create: the party door, even under --screenshot
    bool start_journal = false;  // --journal: open the journal at once
    bool start_eye = false;      // --eye: Wizard Eye lit at master, for reproducing
    int walk_from = -1;      // walk the next event from this sequence, not the top
    int ask_event = -1;      // the event whose question awaits an answer
    game::WalkOutcome::Ask ask_pending;
    std::string ask_typed;
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
        } else if (a == "--time" && i + 1 < argc) {
            start_hour = std::atoi(argv[++i]);
        } else if (a == "--shop" && i + 1 < argc) {
            start_shop = std::atoi(argv[++i]);
        } else if (a == "--walk" && i + 1 < argc) {
            walk_on_start = std::atoi(argv[++i]);
        } else if (a == "--create") {
            force_create = true;
        } else if (a == "--journal") {
            start_journal = true;
        } else if (a == "--eye") {
            start_eye = true;
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

        // The same face bounds the game bakes, so the bench measures the
        // culled path it actually runs.
        struct BenchBound {
            render::Vec3 center{};
            float radius = 0.0f;
        };
        std::vector<BenchBound> bench_bounds(session.blv.faces.size());
        for (std::size_t i = 0; i < session.blv.faces.size(); ++i) {
            const auto& face = session.blv.faces[i];
            if (face.vertex_ids.empty()) {
                continue;
            }
            float cx = 0, cy = 0, cz = 0;
            for (const std::uint16_t v : face.vertex_ids) {
                const auto& p = session.blv.vertices[v];
                cx += p.x;
                cy += p.y;
                cz += p.z;
            }
            const auto n = static_cast<float>(face.vertex_ids.size());
            const render::Vec3 center = world::to_render_space(
                static_cast<int>(cx / n), static_cast<int>(cy / n), static_cast<int>(cz / n));
            float radius = 0.0f;
            for (const std::uint16_t v : face.vertex_ids) {
                const auto& p = session.blv.vertices[v];
                const render::Vec3 at = world::to_render_space(p.x, p.y, p.z);
                const float dx = at.x - center.x;
                const float dy = at.y - center.y;
                const float dz = at.z - center.z;
                radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            bench_bounds[i] = {center, radius};
        }

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
                draw_indoor(bench_scene, session, cache, bench_light, 1.0f, nullptr,
                            [&](std::size_t index) {
                                return index < bench_bounds.size() &&
                                       bench_bounds[index].radius > 0.0f &&
                                       !bench_scene.might_see(bench_bounds[index].center,
                                                              bench_bounds[index].radius);
                            });
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
            // Opened even with no ambient decorations: doors and switches
            // play their one-shots through the same mixer.
            if (ambient.open(*install) && !ambient_sources.empty()) {
                std::cout << ambient_sources.size() << " decorations make a sound\n";
            }
        }
    }

    // Indoors a fixed overhead lamp, since a level has no sky; outdoors the
    // sun, which moves with the clock (see src/game/daylight.hpp).
    const render::Vec3 lamp = render::normalize(render::Vec3{0.3f, 1.0f, 0.2f});
    const render::Color indoor_sky{16, 16, 24, 255};

    const image::Font font = load_font(data_dir, "Lucida.fnt");

    data::ItemStatsTable item_stats;
    (void)data::load_item_stats(data_dir, item_stats);
    // What using a thing does: the herbs' and potions' own table.
    data::UseItemTable use_items;
    (void)data::load_use_items(data_dir, use_items);
    data::SpellStatsTable spell_stats;
    (void)data::load_spell_stats(data_dir, spell_stats);

    // The party. Its names come from the game's own list; its numbers do not
    // come from anywhere, because no shipped table holds them. See
    // src/game/party.hpp.
    data::NameTable given_names;
    (void)data::load_names(data_dir, given_names);
    data::ProfessionTextTable trade_talk;
    (void)data::load_profession_text(data_dir, trade_talk);
    data::NpcProfessionTable professions;
    (void)data::load_npc_professions(data_dir, professions);
    data::NpcNewsTable rumors;
    (void)data::load_npc_news(data_dir, rumors);
    data::DescriptionTable stat_descriptions;
    data::DescriptionTable class_descriptions;
    (void)data::load_descriptions(data_dir, "stats.txt", stat_descriptions);
    (void)data::load_descriptions(data_dir, "Class.txt", class_descriptions);
    data::JournalTable award_texts;
    (void)data::load_awards(data_dir, award_texts);
    data::JournalTable quest_texts;
    (void)data::load_quests(data_dir, quest_texts);
    data::DescriptionTable skill_table;
    (void)data::load_descriptions(data_dir, "SkillDes.txt", skill_table);
    std::array<game::Character, 4> party = game::make_party(given_names, 1);
    // The opening quest's own seed: bit 81's designers' note reads "Set
    // when the party starts", and its journal line is "Show Sulman's letter
    // to Andover Potbello" — so a fresh party begins holding The Letter
    // (item 505) with the bit lit. `observed` for the note; the seeding
    // site is the engine's.
    bool seed_start = true;
    // The party is the player's to shape before the world starts: class,
    // face and name from the game's own tables and portraits, the numbers
    // rerolled at will. Every tooling flag means "get to the world", so any
    // of them skips the door.
    bool creating = force_create || (screenshot.empty() && bench_frames == 0 &&
                                     walk_on_start < 0 && start_shop == 0 && open_sheet == 0 &&
                                     open_pack == 0);
    int create_slot = 0;
    Mm6Random create_random{0x51C7E3A9u};
    std::array<game::Pack, 4> packs;
    int shown_member = open_sheet >= 1 && open_sheet <= 4 ? open_sheet - 1 : -1;
    int shown_pack = open_pack >= 1 && open_pack <= 4 ? open_pack - 1 : -1;
    std::string pick_up_message;
    std::uint64_t pick_up_shown = 0;

    // Trading. The directory picks a shop; the counter is the screen.
    data::MerchantTextTable merchant_words;
    (void)data::load_merchant_text(data_dir, merchant_words);
    data::BuildingStatsTable all_buildings;
    (void)data::load_building_stats(data_dir, all_buildings);
    // The stables and docks name their destinations by area code and display
    // name, so the routes resolve through the map table.
    data::MapStatsTable map_stats;
    (void)data::load_map_stats(data_dir, map_stats);
    data::RandomItemTable random_items;
    data::StandardBonusTable standard_bonuses;
    data::SpecialBonusTable special_bonuses;
    (void)data::load_random_items(data_dir, random_items);
    (void)data::load_standard_bonuses(data_dir, standard_bonuses);
    (void)data::load_special_bonuses(data_dir, special_bonuses);
    auto shops_here = all_buildings.on_map(data::map_code_of(session.file_name));

    int gold = game::kStartingGold;
    // Food rations, the counter quest events give to and take from and a
    // camp eats. A week's worth to start with is this engine's own choice;
    // the tables do not say.
    int party_food = 7;
    int bank_gold = 0;  // what the vault keeps; no table pays interest
    int open_shop = -1;  // an index into shops_here, or none
    std::vector<game::StockItem> shop_stock;
    std::string shop_said;
    std::set<int> opened_chests;  // a chest gives up its contents once

    std::int64_t last_poison_hour = 0;  // when poison last gnawed
    std::int64_t last_hire_day = 0;  // when the followers last did their daily work

    // A die for what is neither combat's nor a map's: potion explosions.
    Mm6Random misc_random{0xA1C4E317u};

    // The event walker's memory: quest bits and event variables, which are
    // the party's rather than any map's, so they survive travelling.
    game::WalkState script_state;
    if (seed_start) {
        script_state.bits.insert(81);
        (void)packs[0].add(505, 2, 2);  // The Letter, at its scroll art's size
    }

    // The shared quest script: 66 of the 88 face event ids that no map's own
    // script defines are GLOBAL.EVT events, and 170 of the 298 NPC topic ids
    // are too — topic id, `npctext.txt` row and global event share one id
    // space. A face falls back to it, and asking a quest giver about a topic
    // runs its event. See docs/formats/map-events.md.
    world::MapScript global_script;
    {
        lod::LodArchive icons_archive;
        std::span<const std::byte> raw;
        if (lod::LodArchive::open(data_dir / "icons.lod", icons_archive) == lod::LodError::None &&
            icons_archive.payload("GLOBAL.EVT", raw) == lod::LodArchive::PayloadError::None) {
            (void)world::MapScript::parse(raw, global_script);
        }
    }

    // Talking. The tables are all decoded; this is the first thing that uses
    // them together. See src/game/conversation.hpp.
    data::InterfaceStrings interface_words;
    (void)data::load_interface_strings(data_dir, interface_words);
    data::NpcDialogueTable dialogue;
    data::NpcPersonalityTable personalities;
    (void)data::load_npc_dialogue(data_dir, dialogue);
    (void)data::load_npc_personalities(data_dir, personalities);
    int talking_to = -1;  // an index into the open shop's people, or none
    // The help the party pays for: at most two, the follower panel's own
    // seat count. Wages fall due weekly, the cost column's own unit.
    std::vector<game::Hireling> hirelings;
    std::set<int> promoted_awards;  // promotion awards already stepped up
    // Spell-borne travel: when flight wears off, where Town Portal may
    // reach (outdoor towns seen, first-visit order), and the Gate Master's
    // once-a-day.
    std::int64_t fly_until = 0;
    std::int64_t torch_until = 0;  // Torch Light's written hours
    std::int64_t eye_until = start_eye ? 1 << 30 : 0;  // Wizard Eye's written hours
    int eye_rank = start_eye ? 2 : 0;  // 0 monsters, 1 + treasure, 2 + interest
    // Lloyd's Beacon: the markers, capped by the caster's rank cell — "1
    // Beacon", "3 Beacons", "5 Beacons" — each decaying on its cell's own
    // clock.
    struct Beacon {
        std::string map;
        render::Vec3 at{};
        std::int64_t until = 0;
    };
    std::vector<Beacon> beacons;
    bool beaconing = false;  // the marker list is open
    int beacon_capacity = 1;
    std::int64_t beacon_decay = 0;  // minutes a fresh marker lives
    std::vector<std::string> visited_towns;
    std::int64_t portal_used_day = -1;
    bool porting = false;  // the destination list is open
    std::int64_t next_wage_day = 7;
    std::string talk_answer;
    std::set<int> approaches_used;  // per conversation: 0 beg, 1 bribe, 2 threat
    int pack_cursor_x = 0, pack_cursor_y = 0;  // the pack screen's chosen cell
    bool show_journal = start_journal;
    std::array<int, 4> known_hp{};        // last seen, to spot fresh wounds
    std::array<std::uint64_t, 4> wince_until{};  // SDL ticks, per member
    // How the world sees the party: reputation moved by deeds, fame worn
    // from experience. The derivation and the deed prices are the engine's
    // own and say so where they act.
    int reputation = 0;
    std::set<int> greeted_npcs;  // who has met the party, this session
    // A passerby stopped in the street: the actor index, and the persona
    // assembled for them — a name from npcnames.txt by the sprite's own
    // gender letter, the Peasant personality's words. The assembly is the
    // engine's; every word is a table's.
    int street_talk = -1;
    std::string street_name;
    bool street_female = false;
    // A town, for Town Portal's purposes, is an outdoor map with counters —
    // the engine's own reading of the spell's "last town visited"; the
    // spell's words pick the destination, only the list is ours.
    const auto note_town = [&] {
        if (!session.outdoor() || shops_here.empty()) {
            return;
        }
        if (std::find(visited_towns.begin(), visited_towns.end(), session.file_name) ==
            visited_towns.end()) {
            visited_towns.push_back(session.file_name);
        }
    };
    note_town();
    const auto standing_for = [&](int npc_id) {
        game::Standing standing;
        standing.reputation = reputation;
        // "Reputation is decreased by one full category" while such help is
        // kept; a category is one greeting band, sized by the engine at 25.
        for (const auto& h : hirelings) {
            if (h.benefit.reputation_drop) {
                standing.reputation -= 25;
            }
        }
        long long total = 0;
        for (const auto& member : party) {
            total += member.experience;
        }
        // Fame as the average thousand of experience: the engine's own
        // derivation, the table naming no other source.
        standing.fame = static_cast<int>(total / 4 / 1000);
        standing.met_before = greeted_npcs.contains(npc_id);
        return standing;
    };

    // The design table's row and the session's building are two views of one
    // establishment, joined by the row id. The people answered are as the
    // quest chain has them: whoever an event moved away is gone, and whoever
    // it moved here from elsewhere on this map stands at this counter. A
    // person moved in from another map cannot be seated — their record is
    // not in this session.
    const auto people_of = [&](const data::BuildingStatsEntry& shop) {
        std::vector<world::SessionNpc> out;
        for (const auto& b : session.buildings) {
            for (const auto& person : b.people) {
                const auto moved = script_state.npc_places.find(person.npc_id);
                const bool placed_here = moved != script_state.npc_places.end()
                                             ? moved->second == shop.id
                                             : b.building_id == shop.id;
                if (placed_here) {
                    out.push_back(person);
                }
            }
        }
        return out;
    };
    // What a counter stocks: a magic guild's shelves are its own row's
    // spell range as books; everything else is generated from its stock
    // spec.
    const auto stock_for = [&](const data::BuildingStatsEntry& shop, std::uint32_t seed) {
        auto stock = game::guild_stock_of(shop, spell_stats, item_stats);
        if (!stock.empty()) {
            return stock;
        }
        return game::stock_of(shop, random_items, item_stats, standard_bonuses, special_bonuses,
                              seed);
    };
    if (start_shop >= 1 && start_shop <= static_cast<int>(shops_here.size())) {
        open_shop = start_shop - 1;
        const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
        shop_stock = stock_for(shop, static_cast<std::uint32_t>(shop.id) * 2654435761U);
    }

    // The monsters start where the map put them and wander from there.
    game::Mob mob;
    mob.reset(session, monster_stats, static_cast<std::uint32_t>(session.actors.size()) + 1u);
    game::Battle battle;
    battle.reset(session, monster_stats, static_cast<std::uint32_t>(session.actors.size()) + 7u);
    float party_recovery = 0.0f;
    // Turn-based: the world holds still and time passes only in rounds a
    // party action spends. The round's one second is the engine's own
    // quantum; everything inside it runs on the tables' numbers as ever.
    bool turn_based = false;
    int save_slot = 1;  // which of the nine files F5 and F9 speak to
    bool pending_round = false;
    constexpr float kRoundSeconds = 1.0f;
    int striker = 0;  // whose turn it is to swing

    // Time, and when this map is next due to refill. The interval is the map's
    // own: see docs/formats/text-tables.md.
    game::GameClock clock;
    if (start_hour >= 0 && start_hour < game::kHoursPerDay) {
        clock = game::GameClock{static_cast<std::int64_t>(start_hour) * game::kMinutesPerHour};
    }
    std::int64_t next_refill = session.refill_days > 0 ? clock.day() + session.refill_days
                                                       : std::numeric_limits<std::int64_t>::max();

    // The animation each monster is drawn in, re-resolved only when the fight
    // changes it: looking a name up through the frame table every frame for
    // every monster is work nothing asked for.
    std::vector<world::MonsterAnimation> shown_kind(session.actors.size(),
                                                    world::MonsterAnimation::Stand);
    std::vector<std::string> shown_animation(session.actors.size());
    std::vector<game::ActiveLaunch> launches;  // sprites a script put in the air

    // A spell the party fired, mid-flight: the bolt carries its blow to the
    // monster it was aimed at and lands it on arrival, the way the script
    // launches fly. The X-variant burst then lingers a moment where it hit.
    struct SpellShot {
        game::ActiveLaunch flight;
        std::size_t target = 0;
        data::SpellRange flat;
        data::SpellRange per_skill;
        int skill = 0;
        std::string element;
        std::string caster;
        std::string burst;  // the X group, empty when the table has none
    };
    std::vector<SpellShot> spell_shots;
    struct SpellBurst {
        std::string animation;
        render::Vec3 position;
        std::uint64_t until = 0;  // SDL ticks
    };
    std::vector<SpellBurst> spell_bursts;

    // The map's own lights, baked per face once per load: the sum of each
    // light's reach at the face's centre. The linear falloff and the floor
    // are this engine's; the positions, radii and brightness are the
    // file's. `inferred` for the curve.
    std::vector<float> face_light;
    // Each face's centre and a radius that encloses it, baked once so the
    // frame loop can skip whole faces the camera cannot see.
    struct FaceBound {
        render::Vec3 center{};
        float radius = 0.0f;
    };
    std::vector<FaceBound> face_bounds;
    const auto bake_lights = [&] {
        face_light.assign(session.blv.faces.size(), 0.0f);
        face_bounds.assign(session.blv.faces.size(), {});
        if (!session.indoor()) {
            return;
        }
        const auto lights = world::extract_lights(session.blv);
        for (std::size_t i = 0; i < session.blv.faces.size(); ++i) {
            const auto& face = session.blv.faces[i];
            if (face.vertex_ids.empty()) {
                continue;
            }
            float cx = 0, cy = 0, cz = 0;
            for (const std::uint16_t v : face.vertex_ids) {
                if (v >= session.blv.vertices.size()) {
                    continue;
                }
                cx += session.blv.vertices[v].x;
                cy += session.blv.vertices[v].y;
                cz += session.blv.vertices[v].z;
            }
            const auto n = static_cast<float>(face.vertex_ids.size());
            cx /= n;
            cy /= n;
            cz /= n;
            // The bound, in renderer axes like everything drawn.
            const render::Vec3 center = world::to_render_space(
                static_cast<int>(cx), static_cast<int>(cy), static_cast<int>(cz));
            float radius = 0.0f;
            for (const std::uint16_t v : face.vertex_ids) {
                if (v >= session.blv.vertices.size()) {
                    continue;
                }
                const auto& p = session.blv.vertices[v];
                const render::Vec3 at = world::to_render_space(p.x, p.y, p.z);
                const float dx = at.x - center.x;
                const float dy = at.y - center.y;
                const float dz = at.z - center.z;
                radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            face_bounds[i] = {center, radius};
            float glow = 0.0f;
            for (const auto& light : lights) {
                const float dx = cx - static_cast<float>(light.x);
                const float dy = cy - static_cast<float>(light.y);
                const float dz = cz - static_cast<float>(light.z);
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float radius = static_cast<float>(light.radius) * 2.0f;
                if (distance < radius) {
                    glow += (1.0f - distance / radius) *
                            (static_cast<float>(light.brightness) / 31.0f);
                }
            }
            face_light[i] = glow > 1.0f ? 1.0f : glow;
        }
    };
    bake_lights();

    render::SceneRenderer scene(kWidth, kHeight);

    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    // Stand a door's vertices where its open flag says, shared by throwing a
    // lever and loading a save. The caller rebuilds collision after the last
    // door it moves.
    const auto move_door = [&](world::MapDoor& door) {
        for (std::size_t i = 0; i < door.vertex_ids.size(); ++i) {
            const std::uint16_t vid = door.vertex_ids[i];
            if (vid >= session.blv.vertices.size()) {
                continue;
            }
            const float slide = door.open ? static_cast<float>(door.distance) : 0.0f;
            auto& v = session.blv.vertices[vid];
            v.x = static_cast<std::int16_t>(door.x_base[i] + static_cast<int>(door.dx * slide));
            v.y = static_cast<std::int16_t>(door.y_base[i] + static_cast<int>(door.dy * slide));
            v.z = static_cast<std::int16_t>(door.z_base[i] + static_cast<int>(door.dz * slide));
        }
    };

    // A person as the quest chain has rewritten them: topic slots overridden
    // by the walked events' own opcode-39 steps, so Andover offers the next
    // thing once the letter is paid for. Where an NPC has been moved is
    // recorded and saved but not yet drawn.
    const auto patched = [&](world::SessionNpc person) {
        for (int slot = 0; slot < 3; ++slot) {
            if (const auto it = script_state.npc_topics.find({person.npc_id, slot});
                it != script_state.npc_topics.end()) {
                person.topics[static_cast<std::size_t>(slot)] = it->second;
            }
        }
        return person;
    };

    // The named conditions a spell can set, matched by the spell's own name
    // to the same four the potions write, for the duration its rank cell
    // states — with the reader's level standing in for the skill. `inferred`
    const auto apply_buff = [&](const data::SpellStatsEntry& spell, game::Character& who,
                                int skill) -> bool {
        const data::SpellDuration duration = data::parse_spell_duration(spell, 0);
        if (duration.empty()) {
            return false;
        }
        const std::int64_t until = clock.minutes() + duration.minutes(skill);
        const std::string name = data::cp1252_to_utf8(spell.name);
        if (name == "Haste") {
            who.haste_until = until;
        } else if (name == "Bless") {
            who.bless_until = until;
        } else if (name == "Heroism") {
            who.heroism_until = until;
        } else if (name == "Stone Skin") {
            who.stone_skin_until = until;
        } else {
            return false;
        }
        return true;
    };

    // A walked event's gives and takes, shared by using a face and talking
    // to a quest giver. Giving returns the item's name for the message line,
    // or nothing when no pack had room.
    const auto give_item = [&](int id) -> std::string {
        const auto* row = item_stats.at(static_cast<std::size_t>(id));
        if (row == nullptr) {
            return {};
        }
        const render::Texture& icon = cache.icon(row->picture);
        const int w = std::max(1, game::cells_across(static_cast<int>(icon.width())));
        const int h = std::max(1, game::cells_across(static_cast<int>(icon.height())));
        for (auto& pack : packs) {
            if (pack.add(id, w, h)) {
                return data::cp1252_to_utf8(row->name);
            }
        }
        return {};
    };
    const auto take_item = [&](int id) {
        for (auto& pack : packs) {
            for (const auto& carried : pack.items()) {
                if (carried.item_id == id) {
                    pack.remove(carried.x, carried.y);
                    return;
                }
            }
        }
    };

    // Lift what a cure spell names from whoever suffers it first. Dead,
    // Stone and Eradicated stay a temple's business, the cures' own prose
    // sending them there. Returns the message, or nothing when nobody
    // suffers what this spell lifts.
    const auto cure_with = [&](const data::SpellStatsEntry& spell,
                               int school_points) -> std::string {
        const data::SpellCure cure = data::parse_spell_cure(spell);
        if (cure.empty()) {
            return {};
        }
        // "If you cast this spell in time": the window grows with skill.
        // An hour of grace per point of the school is the engine's own
        // scale; past it, the prose sends you to a temple, and so does this.
        const std::int64_t window =
            static_cast<std::int64_t>(std::max(1, school_points)) * game::kMinutesPerHour;
        const std::int64_t now = clock.minutes();
        std::string cured;
        bool too_late = false;
        for (auto& member : party) {
            if (member.dead() || member.affliction == "Stone" ||
                member.affliction == "Eradicated") {
                continue;
            }
            bool lifted = false;
            if (cure.poison && member.poisoned > 0) {
                if (now - member.poisoned_minute <= window) {
                    member.poisoned = 0;
                    lifted = true;
                } else {
                    too_late = true;
                }
            }
            if (cure.disease && member.diseased > 0) {
                if (now - member.diseased_minute <= window) {
                    member.diseased = 0;
                    lifted = true;
                } else {
                    too_late = true;
                }
            }
            if (!cure.affliction.empty() &&
                member.affliction.substr(0, cure.affliction.size()) == cure.affliction) {
                if (now - member.affliction_minute <= window) {
                    member.affliction.clear();
                    lifted = true;
                } else {
                    too_late = true;
                }
            }
            if (lifted) {
                cured = member.name;
                // "Awakens all of your characters": sleep lifts from the
                // whole party; the single-target cures stop at the first.
                if (cure.affliction != "Asleep") {
                    break;
                }
            }
        }
        if (!cured.empty()) {
            return data::cp1252_to_utf8(spell.name) + " lifts " + cured + "'s burden";
        }
        if (too_late) {
            return data::cp1252_to_utf8(spell.name) +
                   " comes too late; only a temple can help now";
        }
        return {};
    };

    // The condition a spell lays on a monster, by its Spells.txt row —
    // Charm 61, Mass Fear 62, Slow 81, Paralyze 86, each described in
    // exactly those words — and whether it reaches everything in sight.
    const auto condition_of =
        [](int id) -> std::optional<std::pair<game::MonsterCondition, bool>> {
        switch (id) {
        case 61:
            return std::make_pair(game::MonsterCondition::Charm, false);
        case 62:
            return std::make_pair(game::MonsterCondition::Fear, true);
        case 81:
            return std::make_pair(game::MonsterCondition::Slow, false);
        case 86:
            return std::make_pair(game::MonsterCondition::Paralyze, false);
        default:
            return std::nullopt;
        }
    };
    // "Mass Fear will not work on Undead creatures." Which creatures those
    // are is not a column of any table; reading it off the names is this
    // engine's own. `inferred`
    const auto looks_undead = [](std::string_view name) {
        const std::string low = [&] {
            std::string out;
            for (const char c : name) {
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }();
        for (const std::string_view kind :
             {"skeleton", "zombie", "ghost", "lich", "mummy", "spirit", "bone"}) {
            if (low.find(kind) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    // Parsed skill and enchantment powers, memoized: the tables never
    // change after load, and the equipped sync asks every frame.
    std::map<std::pair<std::string, int>, game::SkillPower> skill_power_memo;
    const auto power_of_skill = [&](const std::string& name, int points) -> game::SkillPower {
        const auto key = std::make_pair(name, points);
        if (const auto it = skill_power_memo.find(key); it != skill_power_memo.end()) {
            return it->second;
        }
        const auto* skill = skill_table.find(name);
        const game::SkillPower power =
            skill != nullptr ? game::skill_power(skill->text, points) : game::SkillPower{};
        skill_power_memo.emplace(key, power);
        return power;
    };
    std::map<std::tuple<int, int, int>, game::EnchantPower> enchant_memo;
    const auto power_of_enchant = [&](int standard, int strength, int special) {
        const auto key = std::make_tuple(standard, strength, special);
        if (const auto it = enchant_memo.find(key); it != enchant_memo.end()) {
            return it->second;
        }
        game::EnchantPower power;
        if (standard > 0) {
            if (const auto* bonus = standard_bonuses.at(static_cast<std::size_t>(standard))) {
                power = game::standard_power(*bonus, strength);
            }
        } else if (special > 0) {
            if (const auto* bonus = special_bonuses.at(static_cast<std::size_t>(special))) {
                power = game::special_power(*bonus);
            }
        }
        enchant_memo.emplace(key, power);
        return power;
    };

    // The weapon skill behind a swing: the striker's points in the equipped
    // weapon's own Skill Group plus the best hired master's, split into what
    // that skill's SKILLDES.TXT lines actually grant.
    const auto weapon_skill_of = [&](const game::Character& who) -> game::SkillPower {
        const int held = who.equipped[static_cast<std::size_t>(game::Slot::Weapon)];
        const auto* row = held > 0 ? item_stats.at(static_cast<std::size_t>(held)) : nullptr;
        if (row == nullptr || row->skill_group.empty()) {
            return {};
        }
        int points = 0;
        if (const auto it = who.skills.find(row->skill_group); it != who.skills.end()) {
            points = it->second;
        }
        int mastered = 0;
        for (const auto& h : hirelings) {
            mastered = std::max(mastered, h.benefit.weapon_skill_bonus);
        }
        points += mastered;
        const auto* skill = skill_table.find(row->skill_group);
        if (points <= 0 || skill == nullptr) {
            return {};
        }
        return game::skill_power(skill->text, points);
    };
    // And the school skill behind a cast: the caster's points in the school's
    // own heading, the hired masters added, their level standing in only
    // when they have no points at all.
    const auto spell_skill_of = [&](const game::Character& who,
                                    const data::SpellStatsEntry& spell) {
        int points = 0;
        if (const auto it = who.skills.find(std::string(game::school_skill(spell.school)));
            it != who.skills.end()) {
            points = it->second;
        }
        int mastered = 0;
        for (const auto& h : hirelings) {
            mastered = std::max(mastered, h.benefit.spell_skill_bonus);
        }
        return points > 0 ? points + mastered : who.level;
    };
    // What the party pays over a counter: the best merchant among them,
    // hired or born, bends the asking price by skills.hpp's own curve.
    const auto haggled = [&](int asking) {
        int best = 0;
        for (const auto& member : party) {
            if (const auto it = member.skills.find("Merchant"); it != member.skills.end()) {
                best = std::max(best, it->second);
            }
        }
        for (const auto& h : hirelings) {
            best = std::max(best, h.benefit.merchant_skill_bonus);
        }
        const auto* skill = skill_table.find("Merchant");
        const int percent =
            skill != nullptr ? game::skill_power(skill->text, best).price_percent : best;
        return game::haggled_price(asking, percent);
    };

    // Whether found loot arrives already known. Trivial things — no ID
    // difficulty in their row — do; the rest need the party's best Identify
    // to reach the item's own number, or a hired Scholar's unlimited eye.
    // Points against difficulty, one for one, is the engine's own
    // comparison. `inferred`
    const auto arrives_identified = [&](const data::ItemStatsEntry& row) {
        if (row.id_rep_st <= 0) {
            return true;
        }
        for (const auto& h : hirelings) {
            if (h.benefit.identifies) {
                return true;
            }
        }
        int best = 0;
        for (const auto& member : party) {
            if (const auto it = member.skills.find("Identify"); it != member.skills.end()) {
                best = std::max(best, it->second);
            }
        }
        return best >= row.id_rep_st;
    };

    // The pack verbs act on the chosen cell first when a pack is open,
    // then fall back to the old first-found walk.
    const auto cursor_first = [&](const game::Pack& pack) {
        std::vector<game::PackedItem> order(pack.items().begin(), pack.items().end());
        if (shown_pack >= 0) {
            if (const auto* under = pack.at(pack_cursor_x, pack_cursor_y); under != nullptr) {
                std::stable_partition(order.begin(), order.end(),
                                      [&](const game::PackedItem& item) {
                                          return item.x == under->x && item.y == under->y;
                                      });
            }
        }
        return order;
    };

    // A walked event's other payments, shared the same way. Experience goes
    // to every member alike — `inferred` from the rewards addressing the
    // party — while a cure or a permanent point lands on the character whose
    // sheet is open, else the first: the original asks who drinks from the
    // barrel, and this shell does not yet. The typed sevens run Speed before
    // Accuracy — the prose join's own order — where the sheet's runs the
    // other way, hence the index maps.
    const auto reward_note = [&](const game::WalkOutcome& outcome) -> std::string {
        static constexpr std::size_t kEventStatOrder[7] = {0, 1, 2, 3, 5, 4, 6};
        static constexpr const char* kStatNames[7] = {"Might",     "Intellect", "Personality",
                                                      "Endurance", "Speed",     "Accuracy",
                                                      "Luck"};
        static constexpr std::size_t kEventResistOrder[5] = {
            static_cast<std::size_t>(data::Resistance::Fire),
            static_cast<std::size_t>(data::Resistance::Electricity),
            static_cast<std::size_t>(data::Resistance::Cold),
            static_cast<std::size_t>(data::Resistance::Poison),
            static_cast<std::size_t>(data::Resistance::Magic)};
        static constexpr const char* kResistNames[5] = {"Fire", "Electricity", "Cold", "Poison",
                                                        "Magic"};
        std::string note;
        const auto add = [&note](const std::string& text) {
            note += (note.empty() ? "" : "  ") + text;
        };
        // The hurts, answered by each victim's own resistance the way the
        // fight's blows are; bare physical goes unresisted.
        for (const auto& harm : outcome.harms) {
            static constexpr std::array<const char*, 6> kElements{"Phys", "Fire", "Elec",
                                                                  "Cold", "Pois", "Magic"};
            const char* type =
                harm.element >= 0 && harm.element < 6 ? kElements[static_cast<std::size_t>(
                                                            harm.element)]
                                                      : "Phys";
            int total = 0;
            const auto hit_one = [&](game::Character& member) {
                if (member.hit_points <= 0) {
                    return;
                }
                const int dealt =
                    game::after_resistance(harm.amount, game::resistance_to(member, type));
                member.hit_points = std::max(0, member.hit_points - dealt);
                total += dealt;
            };
            if (harm.target >= 0 && harm.target <= 3) {
                hit_one(party[static_cast<std::size_t>(harm.target)]);
            } else if (harm.target == 4) {
                hit_one(party[shown_member >= 0 ? static_cast<std::size_t>(shown_member) : 0]);
            } else if (harm.target == 6) {
                hit_one(party[misc_random.next() % party.size()]);
            } else {
                for (auto& member : party) {
                    hit_one(member);
                }
            }
            if (total > 0) {
                add("-" + std::to_string(total) + (harm.element == 0 ? "" : " ") +
                    (harm.element == 0 ? "" : type) + " damage");
            }
        }
        if (outcome.gold_found > 0) {
            // The Factor's and Banker's "bonus on all gold found" rides on
            // exactly what was found, not what was paid.
            int bonus = 0;
            for (const auto& h : hirelings) {
                bonus = std::max(bonus, h.benefit.gold_percent);
            }
            const int extra = outcome.gold_found * bonus / 100;
            script_state.gold += extra;
            add("+" + std::to_string(outcome.gold_found + extra) + " gold found");
        }
        if (script_state.experience > 0) {
            // A hired teacher's percent rides on top; the best one speaks
            // for the party rather than stacking. `inferred`
            int bonus = 0;
            for (const auto& h : hirelings) {
                bonus = std::max(bonus, h.benefit.experience_percent);
            }
            const int paid = script_state.experience + script_state.experience * bonus / 100;
            for (auto& member : party) {
                member.experience += paid;
            }
            add("+" + std::to_string(paid) + " experience");
            script_state.experience = 0;
        }
        if (script_state.food > party_food) {
            add("+" + std::to_string(script_state.food - party_food) + " food");
        }
        party_food = script_state.food;
        auto& who = party[shown_member >= 0 ? static_cast<std::size_t>(shown_member) : 0];
        if (outcome.healed_hp > 0) {
            who.hit_points = std::min(who.max_hit_points, who.hit_points + outcome.healed_hp);
        }
        if (outcome.healed_sp > 0) {
            who.spell_points =
                std::min(who.max_spell_points, who.spell_points + outcome.healed_sp);
        }
        for (std::size_t i = 0; i < 7; ++i) {
            if (outcome.stat_gains[i] > 0) {
                who.attributes[kEventStatOrder[i]] += outcome.stat_gains[i];
                add("+" + std::to_string(outcome.stat_gains[i]) + " " + kStatNames[i] + " for " +
                    who.name);
            }
        }
        for (std::size_t i = 0; i < 5; ++i) {
            if (outcome.resist_gains[i] > 0) {
                who.resistances[kEventResistOrder[i]] += outcome.resist_gains[i];
                add("+" + std::to_string(outcome.resist_gains[i]) + " " + kResistNames[i] +
                    " resistance for " + who.name);
            }
        }
        // A promotion award steps the matching ladder: the award names the
        // class, the class's own prose names the per-level gains.
        for (const int award : script_state.awards) {
            if (!promoted_awards.insert(award).second) {
                continue;
            }
            std::string target;
            for (const auto& row : award_texts.entries()) {
                if (row.bit == award) {
                    target = game::promotion_of(row.text);
                    break;
                }
            }
            if (target.empty()) {
                continue;
            }
            for (auto& member : party) {
                if (game::promotes_to(class_descriptions, member.class_name, target)) {
                    game::promote(member, class_descriptions, target);
                    add(member.name + " is promoted to " + member.class_name);
                }
            }
        }
        return note;
    };

    // Leave this map for another, through the same loader the command line
    // uses. What does not survive the trip is exactly what belongs to the old
    // map: its sounds, its shops, its opened chests, its fight.
    const auto open_map = [&](const std::string& name) -> bool {
        world::MapSession next;
        if (world::load_map_session(game::resolve_games_lod(), data_dir, name, cache, next) !=
            world::MapSessionError::None) {
            return false;
        }
        session = std::move(next);
        ambient_sources.clear();
        for (const auto& d : session.decorations) {
            if (d.sound_id != 0) {
                ambient_sources.push_back({d.position, d.sound_id});
            }
        }
        music.stop();
        if (screenshot.empty() && music_wanted && session.music_track > 0) {
            if (const auto install = platform::install_from_env()) {
                (void)music.start(*install, session.music_track);
            }
        }
        shops_here = all_buildings.on_map(data::map_code_of(session.file_name));
        note_town();
        bake_lights();
        open_shop = -1;
        talking_to = -1;
        shop_stock.clear();
        shop_said.clear();
        opened_chests.clear();
        mob.reset(session, monster_stats, static_cast<std::uint32_t>(session.actors.size()) + 1u);
        battle.reset(session, monster_stats,
                     static_cast<std::uint32_t>(session.actors.size()) + 7u);
        next_refill = session.refill_days > 0 ? clock.day() + session.refill_days
                                              : std::numeric_limits<std::int64_t>::max();
        shown_kind.assign(session.actors.size(), world::MonsterAnimation::Stand);
        shown_animation.assign(session.actors.size(), {});
        launches.clear();
        spell_shots.clear();
        spell_bursts.clear();
        fall_speed = 0.0f;
        SDL_SetWindowTitle(window,
                           ("StarHaven - " + session.title() + " (" + session.file_name + ")")
                               .c_str());
        return true;
    };

    while (running) {
        ++frame;
        music.update();
        ambient.update(camera.position, ambient_sources, session.sounds);
        // The fight's own vocabulary: each monster's attack and death play
        // the `DSOUNDS.BIN` set its DMONLIST record names at +0x08.
        // A shot from range flies the Miss column's kind at the party: the
        // sprite for each kind is this engine's pick from the frame table
        // ("ARRA" for an arrow, the spell bolts for the elements).
        for (const std::size_t shooter : battle.take_shots()) {
            if (shooter >= session.actors.size()) {
                continue;
            }
            const int mid = session.actors[shooter].monster_id;
            const auto* row = mid > 0 && static_cast<std::size_t>(mid) <=
                                             monster_stats.entries().size()
                                  ? &monster_stats.entries()[static_cast<std::size_t>(mid) - 1]
                                  : nullptr;
            if (row == nullptr) {
                continue;
            }
            const std::string_view kind = game::missile_kind(*row);
            const std::string_view sprite = kind == "Arrow"  ? "ARRA"
                                            : kind == "Fire" ? "fire04"
                                            : kind == "Elec" ? "air04"
                                            : kind == "Cold" ? "cold04"
                                            : kind == "Pois" ? "earth04"
                                                             : "dark08";
            game::ActiveLaunch bolt;
            bolt.animation = std::string(sprite);
            bolt.position = session.actors[shooter].position;
            bolt.position.y += 32.0f;
            bolt.target = camera.position;
            launches.push_back(std::move(bolt));
        }
        for (const auto& noise : battle.take_noises()) {
            if (noise.actor >= session.actors.size()) {
                continue;
            }
            const int mid = session.actors[noise.actor].monster_id;
            const auto* row = mid > 0 ? session.monsters.at(static_cast<std::size_t>(mid) - 1)
                                      : nullptr;
            if (row == nullptr || noise.action < 0 ||
                noise.action >= static_cast<int>(row->sounds.size()) ||
                row->sounds[static_cast<std::size_t>(noise.action)] == 0) {
                continue;
            }
            // The record states each of the four ids outright; the Guards'
            // fidget skips one, so arithmetic from the base would miss it.
            if (const auto* sound = session.sounds.find(static_cast<std::uint32_t>(
                    row->sounds[static_cast<std::size_t>(noise.action)]))) {
                ambient.play_once(sound->name);
            }
        }

        bool want_strike = false;
        bool want_rest = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                if (ask_event >= 0) {
                    ask_event = -1;  // the question can simply be walked away from
                } else {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && creating) {
                // Shaping the party: a slot, then its class, face, name and
                // numbers. Enter opens the world.
                auto& who = party[static_cast<std::size_t>(create_slot)];
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4) {
                    create_slot = static_cast<int>(event.key.key - SDLK_1);
                } else if (event.key.key == SDLK_C) {
                    const auto at = std::find(game::kBaseClasses.begin(),
                                              game::kBaseClasses.end(), who.class_name);
                    const auto next = at == game::kBaseClasses.end() ||
                                              at + 1 == game::kBaseClasses.end()
                                          ? game::kBaseClasses.begin()
                                          : at + 1;
                    who.class_name = std::string(*next);
                    game::derive_start(who);
                } else if (event.key.key == SDLK_F) {
                    who.face = (who.face + 1) % game::kFaceCount;
                    who.name = std::string(
                        given_names.name(game::face_is_female(who.face), create_random.next()));
                } else if (event.key.key == SDLK_N) {
                    who.name = std::string(
                        given_names.name(game::face_is_female(who.face), create_random.next()));
                } else if (event.key.key == SDLK_R) {
                    game::roll_attributes(who, create_random);
                    game::derive_start(who);
                } else if (event.key.key == SDLK_RETURN) {
                    creating = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && ask_event >= 0) {
                // A question holds the keys: letters and digits spell the
                // answer, Enter gives it, and the event resumes at the step
                // the answer earns — its own "Wrong!" on a miss.
                const auto key = event.key.key;
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    const auto lower = [](std::string_view text) {
                        std::string out;
                        for (const char c : text) {
                            out += static_cast<char>(
                                std::tolower(static_cast<unsigned char>(c)));
                        }
                        return out;
                    };
                    const auto answer_at = [&](int index) {
                        return index >= 0 && static_cast<std::size_t>(index) <
                                                 session.script_strings.size()
                                   ? lower(session.script_strings.at(
                                         static_cast<std::size_t>(index)))
                                   : std::string();
                    };
                    const std::string given = lower(ask_typed);
                    const bool match = !given.empty() &&
                                       (given == answer_at(ask_pending.answer_a) ||
                                        given == answer_at(ask_pending.answer_b));
                    walk_on_start = ask_event;
                    walk_from = match ? ask_pending.step_on_match : ask_pending.step_on_miss;
                    ask_event = -1;
                } else if (key == SDLK_BACKSPACE && !ask_typed.empty()) {
                    ask_typed.pop_back();
                } else if (key == SDLK_SPACE && ask_typed.size() < 40) {
                    ask_typed += ' ';
                } else if (key >= SDLK_A && key <= SDLK_Z && ask_typed.size() < 40) {
                    ask_typed += static_cast<char>('a' + (key - SDLK_A));
                } else if (key >= SDLK_0 && key <= SDLK_9 && ask_typed.size() < 40) {
                    ask_typed += static_cast<char>('0' + (key - SDLK_0));
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB) {
                show_directory = !show_directory;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_C) {
                shown_member = shown_member < 0 ? 0 : -1;
                shown_pack = -1;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_E &&
                       shown_pack >= 0) {
                // Wear the first thing in this pack that can be worn.
                auto& who = party[static_cast<std::size_t>(shown_pack)];
                auto& pack = packs[static_cast<std::size_t>(shown_pack)];
                for (const auto& carried : cursor_first(pack)) {
                    const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                    if (row == nullptr) {
                        continue;
                    }
                    game::Slot slot = game::slot_for(row->equip_type);
                    if (slot == game::Slot::Count) {
                        continue;
                    }
                    // A one-handed blade may take the left hand — the
                    // Shield slot — when the weapon skill's own line
                    // permits it at the wearer's rank, and the right hand
                    // is already full.
                    if (slot == game::Slot::Weapon &&
                        row->equip_type == data::ItemEquipType::Weapon &&
                        who.equipped[static_cast<std::size_t>(game::Slot::Weapon)] > 0 &&
                        who.equipped[static_cast<std::size_t>(game::Slot::Shield)] == 0 &&
                        !row->skill_group.empty()) {
                        int points = 0;
                        if (const auto it = who.skills.find(row->skill_group);
                            it != who.skills.end()) {
                            points = it->second;
                        }
                        if (const auto* skill = skill_table.find(row->skill_group);
                            skill != nullptr && points > 0 &&
                            game::skill_power(skill->text, points).left_hand) {
                            slot = game::Slot::Shield;
                        }
                    }
                    // What was there comes off and goes back in the pack,
                    // its enchantment with it; the new piece's rides on.
                    const auto si = static_cast<std::size_t>(slot);
                    const int worn = who.equipped[si];
                    const int old_standard = who.worn_standard[si];
                    const int old_strength = who.worn_strength[si];
                    const int old_special = who.worn_special[si];
                    const auto power_of = [&](int std_row, int strength, int special) {
                        game::EnchantPower power;
                        if (std_row > 0) {
                            if (const auto* bonus = standard_bonuses.at(
                                    static_cast<std::size_t>(std_row))) {
                                power = game::standard_power(*bonus, strength);
                            }
                        } else if (special > 0) {
                            if (const auto* bonus =
                                    special_bonuses.at(static_cast<std::size_t>(special))) {
                                power = game::special_power(*bonus);
                            }
                        }
                        return power;
                    };
                    // Hit and spell point grants move the maxima at the
                    // moment of dressing, so they survive saves untouched.
                    const game::EnchantPower off =
                        power_of(old_standard, old_strength, old_special);
                    who.max_hit_points -= off.hit_points;
                    who.hit_points = std::min(who.hit_points, who.max_hit_points);
                    who.max_spell_points -= off.spell_points;
                    who.spell_points = std::min(who.spell_points, who.max_spell_points);
                    const int old_charges = who.worn_charges[si];
                    who.equipped[si] = carried.item_id;
                    who.worn_standard[si] = carried.standard_bonus;
                    who.worn_strength[si] = carried.standard_strength;
                    who.worn_special[si] = carried.special_bonus;
                    who.worn_charges[si] =
                        carried.charges > 0
                            ? carried.charges
                            : (row->equip_type == data::ItemEquipType::Wand ? row->modifier_2
                                                                            : 0);
                    const game::EnchantPower on =
                        power_of(carried.standard_bonus, carried.standard_strength,
                                 carried.special_bonus);
                    who.max_hit_points += on.hit_points;
                    who.hit_points += on.hit_points;
                    who.max_spell_points += on.spell_points;
                    who.spell_points += on.spell_points;
                    pack.remove(carried.x, carried.y);
                    if (worn > 0) {
                        const auto* old = item_stats.at(static_cast<std::size_t>(worn));
                        const render::Texture& icon =
                            old == nullptr ? cache.icon("") : cache.icon(old->picture);
                        (void)pack.add(
                            worn, std::max(1, game::cells_across(static_cast<int>(icon.width()))),
                            std::max(1, game::cells_across(static_cast<int>(icon.height()))),
                            true, old_standard, old_strength, old_special, old_charges);
                    }
                    pick_up_message = who.name + " wears the " + data::cp1252_to_utf8(row->name);
                    pick_up_shown = SDL_GetTicks();
                    break;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F5) {
                // Save: the engine's own format, next to where it was run.
                game::SaveState state;
                state.map_file = session.file_name;
                state.x = camera.position.x;
                state.y = camera.position.y;
                state.z = camera.position.z;
                state.yaw = camera.yaw;
                state.pitch = camera.pitch;
                state.minutes = clock.minutes();
                state.gold = gold;
                state.bank_gold = bank_gold;
                state.food = party_food;
                for (const auto& h : hirelings) {
                    state.hired.push_back({h.npc_id, h.profession_id, h.name});
                }
                state.wage_day = next_wage_day;
                state.awards.assign(script_state.awards.begin(), script_state.awards.end());
                state.visited_towns = visited_towns;
                state.fly_until = fly_until;
                state.reputation = reputation;
                state.torch_until = torch_until;
                state.eye_until = eye_until;
                state.eye_rank = eye_rank;
                for (const auto& beacon : beacons) {
                    state.beacons.push_back(
                        {beacon.map, beacon.at.x, beacon.at.y, beacon.at.z, beacon.until});
                }
                state.bits = script_state.bits;
                state.variables = script_state.variables;
                state.npc_topics = script_state.npc_topics;
                state.npc_places = script_state.npc_places;
                state.party = party;
                for (std::size_t i = 0; i < packs.size(); ++i) {
                    state.packs[i] = packs[i].items();
                }
                state.opened_chests.assign(opened_chests.begin(), opened_chests.end());
                for (const auto& door : session.doors) {
                    if (door.open) {
                        state.open_doors.push_back(door.id);
                    }
                }
                std::ofstream file(save_slot_path(save_slot));
                file << game::save_text(state);
                pick_up_message = file.good()
                                      ? "Saved to slot " + std::to_string(save_slot)
                                      : "Could not write the save";
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F6) {
                // The next slot, with what it holds: same format, numbered
                // files, the map and the day read off the save itself.
                save_slot = save_slot % 9 + 1;
                std::ifstream file(save_slot_path(save_slot));
                std::stringstream buffer;
                buffer << file.rdbuf();
                game::SaveState peek;
                if (file.good() && game::parse_save(buffer.str(), peek)) {
                    std::string title = peek.map_file;
                    for (const auto& m : map_stats.entries()) {
                        if (m.file_name == peek.map_file && !m.name.empty()) {
                            title = m.name;
                            break;
                        }
                    }
                    pick_up_message = "Slot " + std::to_string(save_slot) + ": " +
                                      data::cp1252_to_utf8(title) + ", day " +
                                      std::to_string(peek.minutes /
                                                     (game::kMinutesPerHour *
                                                      game::kHoursPerDay) +
                                                     1);
                } else {
                    pick_up_message = "Slot " + std::to_string(save_slot) + ": empty";
                }
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F9) {
                // Load: back to the saved map, standing where the party stood.
                game::SaveState state;
                std::ifstream file(save_slot_path(save_slot));
                std::stringstream buffer;
                buffer << file.rdbuf();
                if (!file.good() || !game::parse_save(buffer.str(), state) ||
                    !open_map(state.map_file)) {
                    pick_up_message = "Nothing to load";
                    pick_up_shown = SDL_GetTicks();
                } else {
                    camera.position = {state.x, state.y, state.z};
                    camera.yaw = state.yaw;
                    camera.pitch = state.pitch;
                    clock = game::GameClock{state.minutes};
                    next_refill = session.refill_days > 0
                                      ? clock.day() + session.refill_days
                                      : std::numeric_limits<std::int64_t>::max();
                    gold = state.gold;
                    bank_gold = state.bank_gold;
                    party_food = state.food;
                    hirelings.clear();
                    for (const auto& h : state.hired) {
                        const auto* row = professions.at(h.profession_id);
                        if (row == nullptr) {
                            continue;
                        }
                        game::Hireling hire;
                        hire.npc_id = h.npc_id;
                        hire.name = h.name;
                        hire.profession_id = h.profession_id;
                        hire.profession = data::cp1252_to_utf8(row->name);
                        hire.weekly_cost = row->hire_cost;
                        hire.benefit = game::parse_benefit(row->party_benefit);
                        hirelings.push_back(std::move(hire));
                    }
                    next_wage_day = state.wage_day > 0 ? state.wage_day : clock.day() + 7;
                    last_hire_day = clock.day();
                    script_state.awards = std::set<int>(state.awards.begin(), state.awards.end());
                    promoted_awards = script_state.awards;
                    visited_towns = state.visited_towns;
                    fly_until = state.fly_until;
                    reputation = state.reputation;
                    torch_until = state.torch_until;
                    eye_until = state.eye_until;
                    eye_rank = state.eye_rank;
                    beacons.clear();
                    for (const auto& beacon : state.beacons) {
                        beacons.push_back({beacon.map,
                                           {beacon.x, beacon.y, beacon.z},
                                           beacon.until});
                    }
                    note_town();
                    script_state.bits = state.bits;
                    script_state.variables = state.variables;
                    script_state.npc_topics = state.npc_topics;
                    script_state.npc_places = state.npc_places;
                    party = state.party;
                    for (std::size_t i = 0; i < packs.size(); ++i) {
                        packs[i].clear();
                        for (const auto& item : state.packs[i]) {
                            (void)packs[i].place(item);
                        }
                    }
                    opened_chests =
                        std::set<int>(state.opened_chests.begin(), state.opened_chests.end());
                    bool doors_moved = false;
                    for (const std::uint32_t id : state.open_doors) {
                        for (auto& door : session.doors) {
                            if (door.id == id) {
                                door.open = true;
                                move_door(door);
                                doors_moved = true;
                                break;
                            }
                        }
                    }
                    if (doors_moved) {
                        world::rebuild_indoor_collision(session);
                    }
                    pick_up_message = "Loaded";
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_U &&
                       shown_pack >= 0) {
                // Drink or eat the first thing the use table knows. The
                // effect is the table's own; what this engine applies of it
                // is the hit- and spell-point cures, and the item is removed
                // or becomes what the table says — an emptied bottle.
                auto& who = party[static_cast<std::size_t>(shown_pack)];
                auto& pack = packs[static_cast<std::size_t>(shown_pack)];
                for (const auto& carried : cursor_first(pack)) {
                    // A spell book first: the USEITEMS header's own rule —
                    // the character learns the spell and the book is spent,
                    // or nothing happens if it is already known. That only
                    // casters read is this engine's stand-in for the magic
                    // skill group it asks for. `inferred`
                    if (const auto* row =
                            item_stats.at(static_cast<std::size_t>(carried.item_id));
                        row != nullptr && row->equip_type == data::ItemEquipType::Book) {
                        const int spell_id = data::scroll_spell_of(row->modifier_1);
                        const auto* spell = spell_stats.at(static_cast<std::size_t>(spell_id));
                        if (spell == nullptr) {
                            continue;
                        }
                        if (who.max_spell_points <= 0) {
                            pick_up_message = who.name + " cannot learn spells";
                        } else if (!who.known_spells.insert(spell_id).second) {
                            pick_up_message =
                                who.name + " already knows " + data::cp1252_to_utf8(spell->name);
                        } else {
                            const auto used = carried;
                            pack.remove(used.x, used.y);
                            pick_up_message =
                                who.name + " learns " + data::cp1252_to_utf8(spell->name);
                        }
                        pick_up_shown = SDL_GetTicks();
                        break;
                    }
                    const auto* use = use_items.find(carried.item_id);
                    if (use == nullptr) {
                        continue;
                    }
                    if (use->cure_hit_points > 0) {
                        who.hit_points =
                            std::min(who.max_hit_points, who.hit_points + use->cure_hit_points);
                    }
                    if (use->cure_spell_points > 0) {
                        who.spell_points = std::min(who.max_spell_points,
                                                    who.spell_points + use->cure_spell_points);
                    }
                    // The tables' own temporary amounts and hours; that the
                    // untimed ones last until a rest is this engine's.
                    if (use->temp_stats > 0) {
                        for (auto& bonus : who.temp_attributes) {
                            bonus = std::max(bonus, use->temp_stats);
                        }
                    }
                    if (use->temp_armor > 0) {
                        who.temp_armor = std::max(who.temp_armor, use->temp_armor);
                    }
                    if (use->temp_resistances > 0) {
                        for (auto& bonus : who.temp_resistances) {
                            bonus = std::max(bonus, use->temp_resistances);
                        }
                    }
                    if (use->sets_poison > 0) {
                        who.poisoned = std::max(who.poisoned, use->sets_poison);
                    }
                    if (use->cures_poison) {
                        who.poisoned = 0;
                        if (use->effect.find("all Conditions") != std::string::npos) {
                            who.diseased = 0;
                            who.affliction.clear();
                        }
                    }
                    if (use->buff_hours > 0) {
                        const std::int64_t until =
                            clock.minutes() + static_cast<std::int64_t>(use->buff_hours) * 60;
                        if (use->buff == "Haste") {
                            who.haste_until = until;
                        } else if (use->buff == "Bless") {
                            who.bless_until = until;
                        } else if (use->buff == "Heroism") {
                            who.heroism_until = until;
                        } else if (use->buff == "Stone Skin") {
                            who.stone_skin_until = until;
                        }
                    }
                    pick_up_message = who.name + " \x97 " + data::cp1252_to_utf8(use->name) +
                                      ": " + data::cp1252_to_utf8(use->effect);
                    pick_up_shown = SDL_GetTicks();
                    const auto used = carried;  // remove() invalidates the ref
                    if (use->removed_when_used || use->becomes_item > 0) {
                        pack.remove(used.x, used.y);
                        if (use->becomes_item > 0) {
                            (void)pack.place(use->becomes_item, used.x, used.y, used.width,
                                             used.height);
                        }
                    }
                    break;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_M &&
                       shown_pack >= 0) {
                // Pour the first potion into the second, the way the mixing
                // matrix answers: a new potion in the second's cell and an
                // emptied bottle where the first stood, or the explosion the
                // sheet grades — its own fire damage on the mixer, both
                // bottles gone.
                auto& who = party[static_cast<std::size_t>(shown_pack)];
                auto& pack = packs[static_cast<std::size_t>(shown_pack)];
                std::vector<game::PackedItem> known;
                for (const auto& carried : cursor_first(pack)) {
                    if (use_items.find(carried.item_id) != nullptr && known.size() < 2) {
                        known.push_back(carried);
                    }
                }
                if (known.size() < 2) {
                    pick_up_message = "Mixing takes two things the alchemy knows";
                    pick_up_shown = SDL_GetTicks();
                } else {
                    const data::MixResult mixed =
                        use_items.mix(known[0].item_id, known[1].item_id);
                    if (mixed.kind == data::MixKind::None) {
                        pick_up_message = "They do not combine";
                    } else if (mixed.kind == data::MixKind::Item) {
                        const auto* source = use_items.find(known[0].item_id);
                        pack.remove(known[0].x, known[0].y);
                        if (source != nullptr && source->becomes_item > 0) {
                            (void)pack.place(source->becomes_item, known[0].x, known[0].y,
                                             known[0].width, known[0].height);
                        }
                        pack.remove(known[1].x, known[1].y);
                        (void)pack.place(mixed.item_id, known[1].x, known[1].y, known[1].width,
                                         known[1].height);
                        const auto* result = use_items.find(mixed.item_id);
                        pick_up_message =
                            "You mix " +
                            (result != nullptr ? data::cp1252_to_utf8(result->name)
                                               : std::to_string(mixed.item_id));
                    } else {
                        pack.remove(known[0].x, known[0].y);
                        pack.remove(known[1].x, known[1].y);
                        const auto& grade = data::kExplosionGrades[static_cast<std::size_t>(
                            std::clamp(mixed.explosion_grade, 1, 4) - 1)];
                        if (grade.high > 0) {
                            const int damage =
                                grade.low +
                                static_cast<int>(misc_random.next() %
                                                 static_cast<unsigned>(grade.high - grade.low +
                                                                       1));
                            who.hit_points = std::max(0, who.hit_points - damage);
                            pick_up_message = "The mixture explodes for " +
                                              std::to_string(damage) + " fire damage";
                        } else {
                            who.hit_points = 0;
                            pick_up_message = "The mixture eradicates " + who.name;
                        }
                    }
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_I) {
                shown_pack = shown_pack < 0 ? 0 : -1;
                shown_member = -1;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_T &&
                       open_shop < 0 && !creating) {
                // Stop a passerby: any of the monster table's own civilians
                // — the hostility-zero Peasant rows — within reach.
                if (street_talk >= 0) {
                    greeted_npcs.insert(100000 + street_talk);
                    street_talk = -1;
                    approaches_used.clear();
                    talk_answer.clear();
                } else {
                    const std::size_t who = game::aimed_actor(
                        session, battle, camera.position, camera.forward(), game::kPartyReach);
                    if (who != game::kNoActor && battle.alive(who)) {
                        const int mid = session.actors[who].monster_id;
                        const auto* row =
                            mid > 0 && static_cast<std::size_t>(mid) <=
                                           monster_stats.entries().size()
                                ? &monster_stats.entries()[static_cast<std::size_t>(mid) - 1]
                                : nullptr;
                        if (row != nullptr && row->hostility == 0) {
                            street_talk = static_cast<int>(who);
                            street_female = row->picture.find('F') != std::string::npos;
                            street_name = std::string(given_names.name(
                                street_female,
                                static_cast<std::uint32_t>(who + 1) * 2654435761U));
                            approaches_used.clear();
                            talk_answer.clear();
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_T &&
                       open_shop >= 0) {
                // Talk to whoever the NPC table puts in this establishment.
                if (talking_to >= 0) {
                    const auto met =
                        people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
                    if (talking_to < static_cast<int>(met.size())) {
                        greeted_npcs.insert(met[static_cast<std::size_t>(talking_to)].npc_id);
                    }
                }
                talking_to = talking_to >= 0 ? -1 : 0;
                talk_answer.clear();
                approaches_used.clear();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_B &&
                       open_shop >= 0) {
                open_shop = -1;
                shop_said.clear();
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_F) {
                // Mend what monsters broke: half the item's value per piece,
                // said with the merchant table's own Repair line. The price
                // is this engine's. `inferred`
                // A hired smith mends weapons and an armorer the rest,
                // free, before any bill is drawn up: their rows' own
                // "unlimited repair".
                bool smith = false, armorer = false;
                for (const auto& h : hirelings) {
                    smith = smith || h.benefit.repairs_weapons;
                    armorer = armorer || h.benefit.repairs_armor;
                }
                int mended = 0;
                for (auto& member : party) {
                    for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                        if (!member.equipped_broken[slot]) {
                            continue;
                        }
                        const bool weapon = slot == static_cast<std::size_t>(game::Slot::Weapon);
                        if ((weapon && smith) || (!weapon && armorer)) {
                            member.equipped_broken[slot] = false;
                            ++mended;
                        }
                    }
                }
                int bill = 0;
                for (const auto& member : party) {
                    for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                        if (member.equipped_broken[slot] && member.equipped[slot] > 0) {
                            const auto* row =
                                item_stats.at(static_cast<std::size_t>(member.equipped[slot]));
                            bill += row != nullptr ? std::max(1, row->value / 2) : 1;
                        }
                    }
                }
                if (mended > 0 && bill == 0) {
                    shop_said = "Your followers see to the repairs.";
                } else
                if (bill == 0) {
                    shop_said = "Nothing here is broken.";
                } else if (bill > gold) {
                    shop_said = "Repairs would cost " + std::to_string(bill) + " gold.";
                } else {
                    gold -= bill;
                    for (auto& member : party) {
                        member.equipped_broken.fill(false);
                    }
                    shop_said = std::string(
                        game::merchant_line(merchant_words, data::MerchantAction::Repair, true));
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop < 0 &&
                       (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                       shown_member < 0 && shown_pack < 0 && !porting && !beaconing) {
                // The original's own toggle: the fight in turns.
                turn_based = !turn_based;
                pick_up_message = turn_based ? "The world holds its breath: turn-based"
                                             : "Time flows again: real-time";
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop < 0 &&
                       event.key.key == SDLK_J && !creating) {
                show_journal = !show_journal;
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_J) {
                // Join the guild whose counter this is, gaining its own
                // "Joined the ... Guild" award — worn on the sheet like any
                // honor, and the key the shelves ask for.
                const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
                const game::GuildStock guild = game::parse_guild_stock(shop.stock_a);
                const int dues_award =
                    guild.empty() ? 0 : game::guild_award_of(guild.school, award_texts);
                if (dues_award == 0) {
                    shop_said = "This counter has no rolls to sign.";
                } else if (script_state.awards.contains(dues_award)) {
                    shop_said = "The party already belongs.";
                } else if (gold < game::guild_dues(shop)) {
                    shop_said = "Joining costs " + std::to_string(game::guild_dues(shop)) +
                                " gold.";
                } else {
                    gold -= game::guild_dues(shop);
                    script_state.awards.insert(dues_award);
                    promoted_awards.insert(dues_award);
                    shop_said = "Welcome to the guild.";
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_Y) {
                // Have the counter name what the party could not: every
                // unknown thing in the packs, a tenth of its value each —
                // the price is this engine's, the words the merchant
                // table's own Identify line.
                int bill = 0;
                for (const auto& pack : packs) {
                    for (const auto& carried : pack.items()) {
                        const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                        if (!carried.identified && row != nullptr) {
                            bill += std::max(1, row->value / 10);
                        }
                    }
                }
                if (bill == 0) {
                    shop_said = "Nothing here is a mystery.";
                } else if (bill > gold) {
                    shop_said = "Naming it all would cost " + std::to_string(bill) + " gold.";
                } else {
                    gold -= bill;
                    for (auto& pack : packs) {
                        for (const auto& carried : pack.items()) {
                            if (!carried.identified) {
                                (void)pack.identify_at(carried.x, carried.y);
                            }
                        }
                    }
                    shop_said = std::string(game::merchant_line(
                        merchant_words, data::MerchantAction::Identify, true));
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_S) {
                // Sell the chosen thing when a pack is open — the cursor's
                // cell — else the first thing anyone carries. The counter's
                // offer is bent by the party's Merchant the same way its
                // asking prices are; the bend never passes the item's value.
                const auto sell_from = [&](game::Pack& pack, const game::PackedItem& carried) {
                    const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                    if (row == nullptr) {
                        return false;
                    }
                    const int offer = game::offer_price(*row);
                    const int sweet = row->value - offer;
                    const int best = std::min(
                        row->value,
                        offer + sweet * (row->value - haggled(row->value)) /
                                    std::max(1, row->value));
                    gold += best;
                    pack.remove(carried.x, carried.y);
                    shop_said = std::string(
                        game::merchant_line(merchant_words, data::MerchantAction::Sell, true));
                    return true;
                };
                if (shown_pack >= 0) {
                    auto& pack = packs[static_cast<std::size_t>(shown_pack)];
                    if (const auto* under = pack.at(pack_cursor_x, pack_cursor_y);
                        under != nullptr) {
                        const auto chosen = *under;
                        (void)sell_from(pack, chosen);
                    } else {
                        shop_said = "Nothing under the cursor to sell.";
                    }
                } else {
                    for (auto& pack : packs) {
                        if (pack.empty()) {
                            continue;
                        }
                        const auto carried = pack.items().front();
                        if (sell_from(pack, carried)) {
                            break;
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key >= SDLK_1 &&
                       event.key.key <= SDLK_9) {
                const int chosen = static_cast<int>(event.key.key - SDLK_1);
                if (talking_to >= 0) {
                    // Ask about one of the three things this person knows.
                    // A topic the global script defines is a quest: walk it,
                    // and the event itself decides what is said, paid and
                    // taken — the letter is handed over and the purse fills.
                    const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
                    if (talking_to < static_cast<int>(here.size())) {
                        const auto person =
                            patched(here[static_cast<std::size_t>(talking_to)]);
                        const int id =
                            game::topic_id(person, dialogue, static_cast<std::size_t>(chosen));
                        talk_answer.clear();
                        if (id > 0 && global_script.defines(static_cast<std::uint16_t>(id))) {
                            script_state.gold = gold;
                            script_state.food = party_food;
                            script_state.items.clear();
                            for (const auto& pack : packs) {
                                for (const auto& carried : pack.items()) {
                                    script_state.items.push_back(carried.item_id);
                                }
                            }
                            const game::WalkOutcome outcome = game::walk_event(
                                global_script, static_cast<std::uint16_t>(id), script_state);
                            gold = script_state.gold;
                            if (const std::string rewards = reward_note(outcome);
                                !rewards.empty()) {
                                talk_answer += (talk_answer.empty() ? "" : "  ") + rewards;
                            }
                            for (const int index : outcome.said) {
                                if (const auto* entry = dialogue.at(index); entry != nullptr) {
                                    talk_answer += (talk_answer.empty() ? "" : "  ") +
                                                   data::cp1252_to_utf8(entry->text);
                                }
                            }
                            for (const int given : outcome.given) {
                                if (const std::string name = give_item(given); !name.empty()) {
                                    talk_answer += (talk_answer.empty() ? "You receive "
                                                                        : "  You receive ") +
                                                   name;
                                }
                            }
                            for (const int taken : outcome.taken) {
                                take_item(taken);
                            }
                        }
                        if (talk_answer.empty()) {
                            talk_answer = game::topic_answer(person, dialogue,
                                                             static_cast<std::size_t>(chosen));
                        }
                    }
                } else if (open_shop >= 0 &&
                           game::is_temple(*shops_here[static_cast<std::size_t>(open_shop)]) &&
                           chosen < 5) {
                    // The two verbs the temple's own row names. What a heal
                    // restores — everything this engine tracks — and what a
                    // donation earns — an hour of Bless — are this engine's.
                    const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
                    const int price = game::heal_price(shop);
                    if (chosen == 4) {
                        if (gold >= price) {
                            reputation += 2;  // charity betters the name; the price is ours
                            gold -= price;
                            for (auto& member : party) {
                                member.bless_until =
                                    std::max(member.bless_until, clock.minutes() + 60);
                            }
                            shop_said = "The healer blesses your generosity.";
                        } else {
                            shop_said = "Even a donation takes gold.";
                        }
                    } else {
                        auto& who = party[static_cast<std::size_t>(chosen)];
                        const bool needs = who.hit_points < who.max_hit_points ||
                                           who.spell_points < who.max_spell_points ||
                                           who.poisoned > 0 || who.diseased > 0 ||
                                           !who.affliction.empty();
                        // The stock cell's own ceiling: a temple that lists
                        // "No Dead" sends the corpse elsewhere.
                        const game::TempleService service = game::temple_service(shop);
                        if (!needs) {
                            shop_said = who.name + " needs no healing.";
                        } else if (who.dead() && !service.heals_dead) {
                            shop_said = "This temple cannot raise the dead.";
                        } else if (gold < price) {
                            shop_said = "You cannot afford the healing.";
                        } else {
                            gold -= price;
                            who.hit_points = who.max_hit_points;
                            who.spell_points = who.max_spell_points;
                            who.poisoned = 0;
                            who.diseased = 0;
                            who.affliction.clear();
                            shop_said = who.name + " is made whole.";
                        }
                    }
                } else if (open_shop >= 0 &&
                           game::is_bank(*shops_here[static_cast<std::size_t>(open_shop)]) &&
                           chosen < 4) {
                    // The sheet's own two verbs, in two sizes each.
                    const int amounts[4] = {std::min(100, gold), gold, std::min(100, bank_gold),
                                            bank_gold};
                    const int moved = amounts[chosen];
                    if (chosen < 2) {
                        gold -= moved;
                        bank_gold += moved;
                        shop_said = moved > 0 ? "Deposited " + std::to_string(moved) + " gold."
                                              : "You have nothing to deposit.";
                    } else {
                        bank_gold -= moved;
                        gold += moved;
                        shop_said = moved > 0 ? "Withdrew " + std::to_string(moved) + " gold."
                                              : "The vault holds nothing of yours.";
                    }
                } else if (open_shop >= 0 &&
                           game::is_training(*shops_here[static_cast<std::size_t>(open_shop)]) &&
                           chosen < 4) {
                    // Train, if the hall teaches that high, the experience is
                    // earned, and the fee fits.
                    const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
                    auto& who = party[static_cast<std::size_t>(chosen)];
                    const game::TrainingOffer offer = game::training_offer(shop, who);
                    if (offer.to_level == 0) {
                        shop_said = who.name + " is beyond this hall's teaching.";
                    } else if (offer.experience_needed > 0) {
                        shop_said = who.name + " has not earned it yet.";
                    } else if (offer.cost > gold) {
                        shop_said = "You cannot afford the training.";
                    } else {
                        gold -= offer.cost;
                        game::train(who);
                        // A promoted class's own "extra ... per level", on
                        // this level too.
                        if (const auto* row = class_descriptions.find(who.class_name);
                            row != nullptr && !row->text.empty()) {
                            const game::ClassGains gains =
                                game::parse_class_gains(row->text.front());
                            who.max_hit_points += gains.hp_per_level;
                            who.hit_points = who.max_hit_points;
                            if (who.max_spell_points > 0) {
                                who.max_spell_points += gains.sp_per_level;
                                who.spell_points = who.max_spell_points;
                            }
                        }
                        shop_said = who.name + " reaches level " + std::to_string(who.level) + ".";
                    }
                } else if (open_shop >= 0 &&
                           game::is_travel(*shops_here[static_cast<std::size_t>(open_shop)])) {
                    // Ride, if today is a departure day and the fare fits.
                    const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
                    const auto routes = game::routes_of(shop, map_stats);
                    if (static_cast<std::size_t>(chosen) < routes.size()) {
                        const auto route = routes[static_cast<std::size_t>(chosen)];
                        const int fare = game::fare_of(shop);
                        if (!route.leaves_on(clock.day())) {
                            shop_said = "Nothing leaves for " + route.destination + " today.";
                        } else if (fare > gold) {
                            shop_said = "You cannot afford the fare.";
                        } else {
                            gold -= fare;
                            // A guide shaves land routes, a sailor sea ones,
                            // never below the minimum their own rows state.
                            const bool boat = shop.type == "Boats";
                            int shaved = 0;
                            for (const auto& h : hirelings) {
                                shaved = std::max(shaved, boat ? h.benefit.boat_days_faster
                                                              : h.benefit.coach_days_faster);
                            }
                            const int days = std::max(1, route.days - shaved);
                            clock.advance_hours(days * game::kHoursPerDay);
                            if (open_map(route.map_file)) {
                                camera.position = {0, 32.0f * 30.0f, 0};
                                camera.yaw = 0.6f;
                                camera.pitch = -0.3f;
                                pick_up_message = "After " + std::to_string(days) +
                                                  (days == 1 ? " day" : " days") +
                                                  " you arrive in " + session.title();
                                pick_up_shown = SDL_GetTicks();
                            }
                        }
                    }
                } else if (open_shop >= 0) {
                    // Buy, if the purse and somebody's pack allow it.
                    if (static_cast<std::size_t>(chosen) < shop_stock.size()) {
                        const auto& offered = shop_stock[static_cast<std::size_t>(chosen)];
                        const auto* row = item_stats.at(static_cast<std::size_t>(offered.item_id));
                        const int price = haggled(offered.price);
                        // A guild's shelves serve members: the school's own
                        // "Joined the ... Guild" award, or no sale.
                        const game::GuildStock guild = game::parse_guild_stock(
                            shops_here[static_cast<std::size_t>(open_shop)]->stock_a);
                        if (!guild.empty()) {
                            const int dues_award =
                                game::guild_award_of(guild.school, award_texts);
                            if (dues_award > 0 && !script_state.awards.contains(dues_award)) {
                                shop_said = "Members only. J joins the guild for " +
                                            std::to_string(game::guild_dues(*shops_here
                                                [static_cast<std::size_t>(open_shop)])) +
                                            " gold.";
                                break;
                            }
                        }
                        const bool affordable = row != nullptr && price <= gold;
                        bool carried = false;
                        if (affordable) {
                            const render::Texture& icon = cache.icon(row->picture);
                            const int w =
                                std::max(1, game::cells_across(static_cast<int>(icon.width())));
                            const int h =
                                std::max(1, game::cells_across(static_cast<int>(icon.height())));
                            for (auto& pack : packs) {
                                if (pack.add(offered.item_id, w, h, true,
                                             offered.standard_bonus,
                                             offered.standard_strength,
                                             offered.special_bonus)) {
                                    carried = true;
                                    break;
                                }
                            }
                        }
                        if (carried) {
                            gold -= price;
                            shop_stock.erase(shop_stock.begin() + chosen);
                        }
                        game::Speech counter;
                        counter.speaker = data::cp1252_to_utf8(
                            shops_here[static_cast<std::size_t>(open_shop)]->proprietor);
                        counter.listener = party[0].name;
                        counter.listener_is_female = game::face_is_female(party[0].face);
                        counter.hour = clock.hour();
                        counter.title = shops_here[static_cast<std::size_t>(open_shop)]->title;
                        counter.item =
                            row == nullptr ? std::string{} : data::cp1252_to_utf8(row->name);
                        counter.asking = row == nullptr ? 0 : row->value;
                        counter.offered = price;
                        shop_said = game::substitute(game::merchant_line(merchant_words,
                                                                         data::MerchantAction::Buy,
                                                                         affordable),
                                                     counter, interface_words);
                    }
                } else if (show_directory && chosen < static_cast<int>(shops_here.size())) {
                    // The opens/closes columns finally bar the door: a shut
                    // counter names its hours instead of trading.
                    const auto& shop = *shops_here[static_cast<std::size_t>(chosen)];
                    show_directory = false;
                    if (!clock.open(shop.opens, shop.closes)) {
                        pick_up_message = data::cp1252_to_utf8(shop.name) + " is closed; open " +
                                          std::to_string(shop.opens) + " to " +
                                          std::to_string(shop.closes);
                        pick_up_shown = SDL_GetTicks();
                    } else {
                        open_shop = chosen;
                        shop_stock =
                            stock_for(shop, static_cast<std::uint32_t>(shop.id) * 2654435761U);
                        shop_said.clear();
                    }
                } else if (shown_member >= 0 && chosen >= 4 && chosen < 9) {
                    // Raise the numbered skill, at the staircase's price.
                    auto& who = party[static_cast<std::size_t>(shown_member)];
                    int index = chosen - 4;
                    for (auto& [skill, points] : who.skills) {
                        if (index-- != 0) {
                            continue;
                        }
                        const int cost = game::raise_cost(points);
                        if (who.skill_points >= cost) {
                            who.skill_points -= cost;
                            // "Skill adds to Hit Points" / "...Spell
                            // Points": the raise pays its whole delta at
                            // once, rank doublings included, so the maxima
                            // carry it through saves and promotions alike.
                            const auto* row = skill_table.find(skill);
                            const game::SkillPower before =
                                row != nullptr ? game::skill_power(row->text, points)
                                               : game::SkillPower{};
                            ++points;
                            const game::SkillPower after =
                                row != nullptr ? game::skill_power(row->text, points)
                                               : game::SkillPower{};
                            who.max_hit_points += after.hp_bonus - before.hp_bonus;
                            who.hit_points += after.hp_bonus - before.hp_bonus;
                            if (after.sp_bonus > before.sp_bonus) {
                                who.max_spell_points += after.sp_bonus - before.sp_bonus;
                                who.spell_points += after.sp_bonus - before.sp_bonus;
                            }
                        }
                        break;
                    }
                } else if (shown_member >= 0 && chosen < 4) {
                    shown_member = chosen;
                } else if (shown_pack >= 0 && chosen < 4) {
                    shown_pack = chosen;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_X &&
                       shown_member < 0 && shown_pack < 0 && open_shop < 0) {
                // Read the first spell scroll anyone carries: its own spell,
                // cast once at normal mastery, then the paper is spent. Who
                // reads it and at what skill the scaling part rolls — the
                // first character standing, at their level — is this
                // engine's. `inferred`
                bool read = false;
                // A drawn wand goes first: its row's S-number is the spell,
                // "you must equip it" its own instruction, and a charge
                // burns per cast.
                for (std::size_t who = 0; who < party.size() && !read; ++who) {
                    if (!party[who].can_act() || party[who].afraid()) {
                        continue;
                    }
                    const auto wi = static_cast<std::size_t>(game::Slot::Weapon);
                    const int held = party[who].equipped[wi];
                    const auto* row =
                        held > 0 ? item_stats.at(static_cast<std::size_t>(held)) : nullptr;
                    if (row == nullptr || row->equip_type != data::ItemEquipType::Wand) {
                        continue;
                    }
                    if (party[who].worn_charges[wi] <= 0) {
                        pick_up_message = "The wand is spent";
                        pick_up_shown = SDL_GetTicks();
                        read = true;
                        break;
                    }
                    const int spell_id = data::scroll_spell_of(row->modifier_1);
                    const auto* spell = spell_stats.at(static_cast<std::size_t>(spell_id));
                    if (spell == nullptr) {
                        continue;
                    }
                    const data::SpellEffect effect = data::parse_spell_effect(*spell, 0);
                    std::string what;
                    if (std::string lifted =
                            cure_with(*spell, spell_skill_of(party[who], *spell));
                        !lifted.empty()) {
                        what = party[who].name + " waves the wand: " + lifted;
                    } else if (!effect.damage.empty() || !effect.damage_per_skill.empty()) {
                        const std::size_t target =
                            game::aimed_actor(session, battle, camera.position,
                                              camera.forward(), game::kMissileRange);
                        if (target == game::kNoActor) {
                            pick_up_message = "Nothing in reach to cast at";
                            pick_up_shown = SDL_GetTicks();
                            read = true;
                            break;
                        }
                        what = battle.smite(target, effect.damage, effect.damage_per_skill,
                                            spell_skill_of(party[who], *spell), spell->element,
                                            party[who].name, session, monster_stats, item_stats,
                                            random_items, standard_bonuses, special_bonuses);
                    } else if (const auto lays = condition_of(spell_id)) {
                        const std::size_t single = game::aimed_actor(
                            session, battle, camera.position, camera.forward(),
                            game::kMissileRange);
                        const int seconds =
                            data::parse_spell_duration(*spell, 0).minutes(
                                spell_skill_of(party[who], *spell)) *
                            60;
                        if (single != game::kNoActor &&
                            battle.afflict(single, lays->first,
                                           static_cast<float>(seconds))) {
                            what = party[who].name + " waves the wand: " +
                                   data::cp1252_to_utf8(spell->name) + " takes hold";
                        } else {
                            pick_up_message = "Nothing in reach to cast at";
                            pick_up_shown = SDL_GetTicks();
                            read = true;
                            break;
                        }
                    } else {
                        continue;  // a wand of something this slice cannot cast
                    }
                    --party[who].worn_charges[wi];
                    ambient.play_spell(spell_id);
                    pick_up_message = std::move(what);
                    pick_up_shown = SDL_GetTicks();
                    pending_round = turn_based;
                    read = true;
                }
                for (std::size_t who = 0; who < packs.size() && !read; ++who) {
                    if (!party[who].can_act()) {
                        continue;
                    }
                    for (const auto& carried : packs[who].items()) {
                        const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                        if (row == nullptr || row->equip_type != data::ItemEquipType::SpellScroll) {
                            continue;
                        }
                        const int spell_id = data::scroll_spell_of(row->modifier_1);
                        const auto* spell = spell_stats.at(static_cast<std::size_t>(spell_id));
                        if (spell == nullptr) {
                            continue;
                        }
                        const data::SpellEffect effect = data::parse_spell_effect(*spell, 0);
                        std::string what;
                        if (!effect.heal.empty()) {
                            // The most wounded standing character drinks it in.
                            std::size_t worst = who;
                            int missing = -1;
                            for (std::size_t i = 0; i < party.size(); ++i) {
                                const int gap = party[i].max_hit_points - party[i].hit_points;
                                if (party[i].hit_points > 0 && gap > missing) {
                                    missing = gap;
                                    worst = i;
                                }
                            }
                            const int amount = effect.heal.low;
                            party[worst].hit_points =
                                std::min(party[worst].max_hit_points,
                                         party[worst].hit_points + amount);
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " + party[worst].name +
                                   " is healed";
                        } else if (!effect.damage.empty() || !effect.damage_per_skill.empty()) {
                            const std::size_t target =
                                game::aimed_actor(session, battle, camera.position,
                                                  camera.forward(), game::kPartyReach);
                            if (target == game::kNoActor) {
                                pick_up_message = "Nothing in reach to cast at";
                                pick_up_shown = SDL_GetTicks();
                                read = true;  // keep the scroll: nothing was cast
                                break;
                            }
                            what = battle.smite(target, effect.damage, effect.damage_per_skill,
                                                spell_skill_of(party[who], *spell),
                                                spell->element, party[who].name, session,
                                                monster_stats, item_stats, random_items,
                                                standard_bonuses, special_bonuses);
                        } else if (std::string lifted =
                                       cure_with(*spell, spell_skill_of(party[who], *spell));
                                   !lifted.empty()) {
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " + lifted;
                        } else if (spell_id == 12) {
                            // Wizard Eye: the corner automap, "while
                            // outdoors", for its written hour per point.
                            const int minutes = data::parse_spell_duration(*spell, 0).minutes(
                                spell_skill_of(party[who], *spell));
                            eye_until = std::max(eye_until, clock.minutes() + minutes);
                            eye_rank = std::max(eye_rank, 0);
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) +
                                   ": the corner of the eye opens";
                        } else if (spell_id == 1) {
                            // Torch Light, for its cell's own hour per
                            // point of skill.
                            const int minutes = data::parse_spell_duration(*spell, 0).minutes(
                                spell_skill_of(party[who], *spell));
                            torch_until = std::max(torch_until, clock.minutes() + minutes);
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) +
                                   ": the dark backs off for a while";
                        } else if (spell_id == 33) {
                            // Lloyd's Beacon: the rank cell's own count and
                            // decay — "3 Beacons, decays in 1 day per point
                            // of skill" — then a list, place or recall.
                            const int points = spell_skill_of(party[who], *spell);
                            const int rank = game::rank_of(points);
                            const std::string_view cell = rank >= 2   ? spell->master
                                                          : rank == 1 ? spell->expert
                                                                      : spell->normal;
                            beacon_capacity = std::max(
                                1, data::parse_int(cell.substr(0, cell.find(' ')), 1));
                            const std::size_t decays = cell.find("decays in");
                            beacon_decay =
                                decays == std::string_view::npos
                                    ? static_cast<std::int64_t>(points) * game::kMinutesPerHour
                                    : data::parse_duration_text(cell.substr(decays + 9))
                                          .minutes(points);
                            std::erase_if(beacons, [&](const Beacon& b) {
                                return clock.minutes() >= b.until;
                            });
                            beaconing = true;
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) +
                                   ": the beacons answer";
                        } else if (spell_id == 21) {
                            // Fly, "only works outdoors", for its rank
                            // cell's own minutes per point of skill.
                            if (!session.outdoor()) {
                                pick_up_message = "Fly only works outdoors";
                                pick_up_shown = SDL_GetTicks();
                                read = true;
                                break;
                            }
                            const int minutes = data::parse_spell_duration(*spell, 0).minutes(
                                spell_skill_of(party[who], *spell));
                            fly_until = std::max(fly_until, clock.minutes() + minutes);
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) +
                                   ": the party takes to the air";
                        } else if (spell_id == 31) {
                            // Town Portal, "a 10% chance per point of Water
                            // Magic skill of working", "to the last town
                            // visited" at this normal-rank cast.
                            const int chance =
                                spell_skill_of(party[who], *spell) * 10;
                            if (visited_towns.empty()) {
                                pick_up_message = "The portal finds no town to open on";
                                pick_up_shown = SDL_GetTicks();
                                read = true;  // keep the scroll: nothing was cast
                                break;
                            }
                            if (static_cast<int>(misc_random.next() % 100) >= chance) {
                                what = "The portal fizzles";
                            } else {
                                const std::string town = visited_towns.back();
                                packs[who].remove(carried.x, carried.y);
                                pick_up_message = "The portal opens";
                                pick_up_shown = SDL_GetTicks();
                                if (open_map(town)) {
                                    camera.position = {0, 32.0f * 30.0f, 0};
                                    camera.yaw = 0.6f;
                                    camera.pitch = -0.3f;
                                }
                                read = true;
                                break;
                            }
                        } else if (const auto lays = condition_of(spell_id)) {
                            // Its written minutes per point of skill, on the
                            // fight's own clock.
                            const int seconds =
                                data::parse_spell_duration(*spell, 0).minutes(
                                    spell_skill_of(party[who], *spell)) *
                                60;
                            std::size_t touched = 0;
                            if (lays->second) {
                                // Everything in the caster's sight; how far
                                // sight reaches is this engine's own.
                                for (std::size_t a = 0; a < session.actors.size(); ++a) {
                                    if (!battle.alive(a)) {
                                        continue;
                                    }
                                    const auto& at = session.actors[a].position;
                                    const float dx = at.x - camera.position.x;
                                    const float dz = at.z - camera.position.z;
                                    if (dx * dx + dz * dz > 2048.0f * 2048.0f) {
                                        continue;
                                    }
                                    if (looks_undead(session.actors[a].name)) {
                                        continue;
                                    }
                                    touched += battle.afflict(a, lays->first,
                                                              static_cast<float>(seconds))
                                                   ? 1
                                                   : 0;
                                }
                            } else {
                                const std::size_t single = game::aimed_actor(
                                    session, battle, camera.position, camera.forward(),
                                    game::kPartyReach);
                                if (single != game::kNoActor) {
                                    touched += battle.afflict(single, lays->first,
                                                              static_cast<float>(seconds))
                                                   ? 1
                                                   : 0;
                                }
                            }
                            if (touched == 0) {
                                pick_up_message = "Nothing in reach to cast at";
                                pick_up_shown = SDL_GetTicks();
                                read = true;  // keep the scroll: nothing was cast
                                break;
                            }
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " +
                                   data::cp1252_to_utf8(spell->name) + " takes " +
                                   std::to_string(touched) +
                                   (touched == 1 ? " creature" : " creatures");
                        } else if (apply_buff(*spell, party[who], party[who].level)) {
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " +
                                   data::cp1252_to_utf8(spell->name) +
                                   " for its written time";
                        } else {
                            continue;  // a spell this slice cannot cast yet
                        }
                        packs[who].remove(carried.x, carried.y);
                        ambient.play_spell(spell_id);
                        pick_up_message = std::move(what);
                        pick_up_shown = SDL_GetTicks();
                        read = true;
                        break;
                    }
                }
                pending_round = pending_round || (turn_based && read);
                if (!read) {
                    pick_up_message = "Nobody carries a castable scroll";
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && street_talk >= 0 && open_shop < 0 &&
                       event.key.key >= SDLK_5 && event.key.key <= SDLK_7) {
                // The same three levers, on whoever was stopped: the
                // Peasant personality answers in its own wording.
                const auto* personality = personalities.find("Peasant");
                const int which = static_cast<int>(event.key.key - SDLK_5);
                const game::Speech who{street_name, party[0].name,
                                       game::face_is_female(party[0].face), clock.hour()};
                const auto say = [&](int number) {
                    return personality != nullptr
                               ? game::substitute(data::cp1252_to_utf8(std::string(
                                                      personality->message(number))),
                                                  who, interface_words)
                               : std::string{};
                };
                if (approaches_used.contains(which)) {
                    talk_answer = say(3 + which);
                    if (talk_answer.empty()) {
                        talk_answer = "They have nothing more to say to that.";
                    }
                } else if (which == 1 && gold < 50) {
                    talk_answer = "The party cannot spare a bribe.";
                } else {
                    approaches_used.insert(which);
                    if (which == 1) {
                        gold -= 50;
                    }
                    if (which == 2) {
                        reputation -= 1;
                    }
                    const bool taken =
                        personality != nullptr &&
                        personality->allows_approach(static_cast<data::NpcApproach>(which));
                    if (taken) {
                        talk_answer = say(19 + which * 2);
                        if (!rumors.entries().empty()) {
                            const auto& rumor = rumors.entries()[misc_random.next() %
                                                                 rumors.entries().size()];
                            talk_answer += (talk_answer.empty() ? "" : "  ") +
                                           data::cp1252_to_utf8(rumor.text);
                        }
                    } else {
                        talk_answer = say(20 + which * 2);
                        if (talk_answer.empty()) {
                            talk_answer = "They are unmoved.";
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && talking_to >= 0 && open_shop >= 0 &&
                       event.key.key >= SDLK_5 && event.key.key <= SDLK_7) {
                // Beg, bribe or threaten, answered in the personality's own
                // phrasing: 19/21/23 accept, 20/22/24 refuse, 3/4/5 for
                // asking twice. What a success coaxes — a rumor off the
                // news table — and the fifty-gold bribe are the engine's.
                const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
                if (talking_to < static_cast<int>(here.size())) {
                    const auto person = patched(here[static_cast<std::size_t>(talking_to)]);
                    const auto* personality = personalities.find(person.personality);
                    const int which = static_cast<int>(event.key.key - SDLK_5);
                    const game::Speech who{person.name, party[0].name,
                                           game::face_is_female(party[0].face), clock.hour()};
                    const auto say = [&](int number) {
                        return personality != nullptr
                                   ? game::substitute(data::cp1252_to_utf8(std::string(
                                                          personality->message(number))),
                                                      who, interface_words)
                                   : std::string{};
                    };
                    if (approaches_used.contains(which)) {
                        talk_answer = say(3 + which);
                        if (talk_answer.empty()) {
                            talk_answer = "They have nothing more to say to that.";
                        }
                    } else if (which == 1 && gold < 50) {
                        talk_answer = "The party cannot spare a bribe.";
                    } else {
                        approaches_used.insert(which);
                        if (which == 1) {
                            gold -= 50;
                        }
                        if (which == 2) {
                            reputation -= 1;  // a threat sours the name; ours
                        }
                        const bool taken =
                            personality != nullptr &&
                            personality->allows_approach(static_cast<data::NpcApproach>(which));
                        if (taken) {
                            talk_answer = say(19 + which * 2);
                            if (!rumors.entries().empty()) {
                                const auto& rumor = rumors.entries()[misc_random.next() %
                                                                     rumors.entries().size()];
                                talk_answer += (talk_answer.empty() ? "" : "  ") +
                                               data::cp1252_to_utf8(rumor.text);
                            }
                        } else {
                            talk_answer = say(20 + which * 2);
                            if (talk_answer.empty()) {
                                talk_answer = "They are unmoved.";
                            }
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_H &&
                       talking_to >= 0 && open_shop >= 0) {
                // Hire whoever is being talked to, at their row's weekly
                // cost — or let a follower go. Two seats, the panel's own.
                const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
                if (talking_to < static_cast<int>(here.size())) {
                    const auto person = patched(here[static_cast<std::size_t>(talking_to)]);
                    const auto hired =
                        std::find_if(hirelings.begin(), hirelings.end(), [&](const auto& h) {
                            return h.npc_id == person.npc_id;
                        });
                    const auto* row = professions.at(person.profession_id);
                    if (hired != hirelings.end()) {
                        talk_answer = hired->name + " leaves the party.";
                        hirelings.erase(hired);
                    } else if (row == nullptr) {
                        talk_answer = "They have no trade to offer.";
                    } else if (hirelings.size() >= game::kHirelingLimit) {
                        talk_answer = "The party has followers enough.";
                    } else if (gold < row->hire_cost) {
                        talk_answer = "Their price is " + std::to_string(row->hire_cost) +
                                      " gold a week.";
                    } else {
                        gold -= row->hire_cost;
                        if (hirelings.empty()) {
                            next_wage_day = clock.day() + 7;
                        }
                        game::Hireling hire;
                        hire.npc_id = person.npc_id;
                        hire.name = person.name;
                        hire.profession_id = person.profession_id;
                        hire.profession = data::cp1252_to_utf8(row->name);
                        hire.weekly_cost = row->hire_cost;
                        hire.benefit = game::parse_benefit(row->party_benefit);
                        hirelings.push_back(std::move(hire));
                        talk_answer = row->join_text.empty()
                                          ? person.name + " joins the party."
                                          : data::cp1252_to_utf8(row->join_text);
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_H &&
                       shown_member < 0 && shown_pack < 0 && open_shop < 0) {
                // Cast what somebody knows, at the table's cost and the
                // prose's numbers: the best heal they can afford when anyone
                // is wounded, else the best damage at what the party aims
                // at. Books taught the knowledge; which spell "best" means —
                // the largest parsed amount — is this engine's. `inferred`
                bool cast = false;
                bool wounded = false;
                std::size_t worst = 0;
                int missing = 0;
                for (std::size_t i = 0; i < party.size(); ++i) {
                    const int gap = party[i].max_hit_points - party[i].hit_points;
                    if (party[i].hit_points > 0 && gap > missing) {
                        missing = gap;
                        worst = i;
                        wounded = true;
                    }
                }
                // A spell reaches the missile band, the same range the
                // monsters' own shots use.
                const std::size_t target = game::aimed_actor(
                    session, battle, camera.position, camera.forward(), game::kMissileRange);
                for (auto& caster : party) {
                    if (!caster.can_act() || cast) {
                        continue;
                    }
                    // First, whatever somebody suffers that this caster can
                    // lift: the cures outrank the heals, the way the heals
                    // outrank the smiting.
                    for (const int id : caster.known_spells) {
                        const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                        if (spell == nullptr || caster.spell_points < spell->cost_normal) {
                            continue;
                        }
                        if (std::string lifted =
                                cure_with(*spell, spell_skill_of(caster, *spell));
                            !lifted.empty()) {
                            caster.spell_points -= spell->cost_normal;
                            ambient.play_spell(id);
                            pick_up_message = caster.name + " casts: " + lifted;
                            pick_up_shown = SDL_GetTicks();
                            cast = true;
                            break;
                        }
                    }
                    if (cast) {
                        break;
                    }
                    const data::SpellStatsEntry* best = nullptr;
                    data::SpellEffect best_effect;
                    int best_amount = 0;
                    for (const int id : caster.known_spells) {
                        const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                        if (spell == nullptr || caster.spell_points < spell->cost_normal) {
                            continue;
                        }
                        const int points = spell_skill_of(caster, *spell);
                        const data::SpellEffect effect =
                            data::parse_spell_effect(*spell, game::rank_of(points));
                        const int amount = wounded ? effect.heal.high
                                                   : effect.damage.high +
                                                         effect.damage_per_skill.high * points;
                        if (amount > best_amount &&
                            (wounded ? !effect.heal.empty()
                                     : !effect.damage.empty() ||
                                           !effect.damage_per_skill.empty())) {
                            best = spell;
                            best_effect = effect;
                            best_amount = amount;
                        }
                    }
                    if (best == nullptr) {
                        continue;
                    }
                    if (wounded) {
                        caster.spell_points -= best->cost_normal;
                        ambient.play_spell(best->id);
                        party[worst].hit_points =
                            std::min(party[worst].max_hit_points,
                                     party[worst].hit_points + std::max(1, best_effect.heal.low));
                        pick_up_message = caster.name + " casts " +
                                          data::cp1252_to_utf8(best->name) + " on " +
                                          party[worst].name;
                    } else if (target != game::kNoActor) {
                        caster.spell_points -= best->cost_normal;
                        ambient.play_spell(best->id);
                        // The bolt carries the blow; it lands on arrival.
                        SpellShot shot;
                        shot.target = target;
                        shot.flat = best_effect.damage;
                        shot.per_skill = best_effect.damage_per_skill;
                        shot.skill = spell_skill_of(caster, *best);
                        shot.element = best->element;
                        shot.caster = caster.name;
                        // A school with no projectile group flies unseen
                        // rather than borrowing another school's art.
                        shot.flight.animation =
                            game::spell_sprite_group(best->school, best->number);
                        const std::string burst =
                            game::spell_sprite_group(best->school, best->number, true);
                        if (!session.sprite_frames.group(burst).empty()) {
                            shot.burst = burst;
                        }
                        shot.flight.position = camera.position;
                        shot.flight.target = session.actors[target].position;
                        shot.flight.target.y += 32.0f;
                        pick_up_message = caster.name + " casts " +
                                          data::cp1252_to_utf8(best->name);
                        spell_shots.push_back(std::move(shot));
                    } else {
                        pick_up_message = "Nothing in reach to cast at";
                    }
                    pick_up_shown = SDL_GetTicks();
                    cast = true;
                }
                pending_round = pending_round || (turn_based && cast);
                if (!cast) {
                    pick_up_message = "Nobody can cast what the moment needs";
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE &&
                       shown_member < 0 && shown_pack < 0) {
                want_strike = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_P &&
                       shown_member < 0 && shown_pack < 0 && open_shop < 0) {
                // The Gate Master "casts the Town Portal spell at master
                // ranking once per day" — and master rank "gives choice of
                // destination", so the list opens.
                const bool gated = std::any_of(
                    hirelings.begin(), hirelings.end(),
                    [](const auto& h) { return h.benefit.town_portal; });
                if (!gated) {
                    pick_up_message = "Nobody here can open a portal";
                } else if (portal_used_day == clock.day()) {
                    pick_up_message = "The Gate Master has already cast today";
                } else if (visited_towns.empty()) {
                    pick_up_message = "The portal finds no town to open on";
                } else {
                    porting = !porting;
                    pick_up_message.clear();
                }
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && beaconing &&
                       event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                const auto pick = static_cast<std::size_t>(event.key.key - SDLK_1);
                if (pick < beacons.size()) {
                    // Recall to a standing marker, which burns it.
                    const auto chosen = beacons[pick];
                    beacons.erase(beacons.begin() + static_cast<std::ptrdiff_t>(pick));
                    beaconing = false;
                    ambient.play_spell(33);
                    pick_up_message = "The beacon calls the party back";
                    pick_up_shown = SDL_GetTicks();
                    if (session.file_name == chosen.map || open_map(chosen.map)) {
                        camera.position = chosen.at;
                    }
                } else if (pick == beacons.size() &&
                           static_cast<int>(beacons.size()) < beacon_capacity) {
                    beacons.push_back(
                        {session.file_name, camera.position, clock.minutes() + beacon_decay});
                    beaconing = false;
                    pick_up_message = "A beacon marks this spot";
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && porting &&
                       event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                const auto pick = static_cast<std::size_t>(event.key.key - SDLK_1);
                if (pick < visited_towns.size()) {
                    porting = false;
                    portal_used_day = clock.day();
                    const std::string town = visited_towns[pick];
                    ambient.play_spell(31);
                    pick_up_message = "The portal opens";
                    pick_up_shown = SDL_GetTicks();
                    if (open_map(town)) {
                        camera.position = {0, 32.0f * 30.0f, 0};
                        camera.yaw = 0.6f;
                        camera.pitch = -0.3f;
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && shown_pack >= 0 &&
                       (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT ||
                        event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
                pack_cursor_x += event.key.key == SDLK_RIGHT ? 1
                                 : event.key.key == SDLK_LEFT ? -1
                                                              : 0;
                pack_cursor_y += event.key.key == SDLK_DOWN ? 1
                                 : event.key.key == SDLK_UP ? -1
                                                            : 0;
                pack_cursor_x = std::clamp(pack_cursor_x, 0, game::kPackWidth - 1);
                pack_cursor_y = std::clamp(pack_cursor_y, 0, game::kPackHeight - 1);
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
        in.forward = !creating && keys[SDL_SCANCODE_W];
        in.back = keys[SDL_SCANCODE_S];
        in.left = keys[SDL_SCANCODE_A];
        in.right = keys[SDL_SCANCODE_D];
        in.down = keys[SDL_SCANCODE_Q];
        in.up = keys[SDL_SCANCODE_E];
        in.speed = (SDL_GetModState() & SDL_KMOD_SHIFT) ? 1200.0f : 400.0f;

        // Spell-borne flight counts as the flag while it lasts; the spell's
        // own words keep it outdoors.
        const bool fly_now =
            fly || (session.outdoor() && clock.minutes() < fly_until);
        game::step_player(camera, fall_speed, fly_now, in, session.collision,
                          [&](float x, float z) { return session.terrain_height_at(x, z); });

        // And monsters block the party, the way the party blocks them.
        if (!fly_now) {
            for (std::size_t i = 0; i < session.actors.size(); ++i) {
                if (!battle.alive(i)) {
                    continue;
                }
                camera.position = game::push_out_of(camera.position, session.actors[i].position,
                                                    game::kPartySpacing);
            }
        }

        if (keys[SDL_SCANCODE_LEFT] && shown_pack < 0)
            camera.yaw -= game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_RIGHT] && shown_pack < 0)
            camera.yaw += game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_UP])
            camera.pitch += game::kLookSpeed * in.dt;
        if (keys[SDL_SCANCODE_DOWN])
            camera.pitch -= game::kLookSpeed * in.dt;
        camera.pitch =
            std::clamp(camera.pitch, -render::Camera::kMaxPitch, render::Camera::kMaxPitch);

        // Outdoors, both the sun and the sky follow the clock.
        const render::Color sky = session.outdoor() ? game::sky_colour(clock) : indoor_sky;
        scene.begin(camera, sky);
        if (session.outdoor()) {
            draw_outdoor(scene, session, cache, game::sun_direction(clock),
                         game::light_level(clock));
        } else {
            // "Increases the radius of light": this renderer has no radius,
            // so the lamp itself brightens for the written hours. `inferred`
            draw_indoor(scene, session, cache, lamp,
                        clock.minutes() < torch_until ? 1.45f : 1.0f, &face_light,
                        [&](std::size_t index) {
                            return index < face_bounds.size() &&
                                   face_bounds[index].radius > 0.0f &&
                                   !scene.might_see(face_bounds[index].center,
                                                    face_bounds[index].radius);
                        });
        }
        // Real time flows every frame; turn-based time flows only when a
        // round is owed, one quantum at a time.
        const float sim_dt = turn_based ? (pending_round ? kRoundSeconds : 0.0f) : in.dt;
        pending_round = false;
        if (sim_dt > 0.0f) {
            mob.update(sim_dt, session, camera.position, [&](std::size_t actor) {
                return battle.alive(actor) && battle.can_move(actor);
            });
            if (std::string blow = battle.update(sim_dt, session, monster_stats, spell_stats,
                                                 party, camera.position, clock.minutes());
                !blow.empty()) {
                pick_up_message = std::move(blow);
                pick_up_shown = SDL_GetTicks();
            }
            party_recovery = std::max(0.0f, party_recovery - sim_dt);
            clock.advance_seconds(sim_dt);
            game::advance_launches(launches, sim_dt);
            for (auto& shot : spell_shots) {
                // The bolt tracks its monster the way the fight does; on
                // arrival the blow lands with the spell's own numbers.
                if (shot.target < session.actors.size() && battle.alive(shot.target)) {
                    shot.flight.target = session.actors[shot.target].position;
                    shot.flight.target.y += 32.0f;
                }
                if (!game::advance_launch(shot.flight, sim_dt)) {
                    continue;
                }
                if (!shot.burst.empty()) {
                    spell_bursts.push_back(
                        {shot.burst, shot.flight.target, SDL_GetTicks() + 400});
                }
                if (std::string blow = battle.smite(
                        shot.target, shot.flat, shot.per_skill, shot.skill, shot.element,
                        shot.caster, session, monster_stats, item_stats, random_items,
                        standard_bonuses, special_bonuses);
                    !blow.empty()) {
                    pick_up_message = std::move(blow);
                    pick_up_shown = SDL_GetTicks();
                }
            }
            std::erase_if(spell_shots,
                          [](const SpellShot& s) { return s.flight.arrived; });
            std::erase_if(spell_bursts, [](const SpellBurst& b) {
                return SDL_GetTicks() >= b.until;
            });
        }
        {
            int xp_bonus = 0;
            for (const auto& h : hirelings) {
                xp_bonus = std::max(xp_bonus, h.benefit.experience_percent);
            }
            battle.award(party, xp_bonus);
        }
        // What the kills left: the gold goes to the purse, the items to
        // whichever pack has room, and one line names the lot.
        std::string found_text;
        if (int found = battle.take_gold(); found > 0) {
            int gold_bonus = 0;
            for (const auto& h : hirelings) {
                gold_bonus = std::max(gold_bonus, h.benefit.gold_percent);
            }
            found += found * gold_bonus / 100;
            gold += found;
            found_text = std::to_string(found) + " gold";
        }
        for (const auto& rolled : battle.take_loot()) {
            const int id = rolled.item_id;
            const auto* row = item_stats.at(static_cast<std::size_t>(id));
            if (row == nullptr) {
                continue;
            }
            const render::Texture& icon = cache.icon(row->picture);
            const int w = std::max(1, game::cells_across(static_cast<int>(icon.width())));
            const int h = std::max(1, game::cells_across(static_cast<int>(icon.height())));
            const bool known = arrives_identified(*row);
            for (auto& pack : packs) {
                if (pack.add(id, w, h, known, rolled.standard_bonus,
                             rolled.standard_bonus_strength, rolled.special_bonus,
                             rolled.charges)) {
                    found_text +=
                        (found_text.empty() ? "" : " and ") +
                        data::cp1252_to_utf8(
                            known || row->unidentified_name.empty()
                                ? game::enchanted_name(
                                      row->name,
                                      standard_bonuses.at(static_cast<std::size_t>(
                                          rolled.standard_bonus)),
                                      special_bonuses.at(static_cast<std::size_t>(
                                          rolled.special_bonus)))
                                : std::string(row->unidentified_name));
                    break;
                }
            }
        }
        if (!found_text.empty()) {
            pick_up_message = "You find " + found_text;
            pick_up_shown = SDL_GetTicks();
        }
        if (const int cut = battle.take_stolen(); cut > 0) {
            gold = std::max(0, gold - cut);
        }

        // Poison gnaws by the hour: its level in hit points, down to the
        // floor but not through it — the last point stands until a cure or
        // the wound that finishes it. The rate is this engine's; the levels
        // are the tables'. `inferred`
        if (clock.minutes() / 60 > last_poison_hour) {
            last_poison_hour = clock.minutes() / 60;
            for (auto& member : party) {
                if (member.poisoned > 0 && member.hit_points > 1) {
                    member.hit_points = std::max(1, member.hit_points - member.poisoned);
                }
                // Disease is poison's slower sibling: half the pace.
                if (member.diseased > 0 && last_poison_hour % 2 == 0 &&
                    member.hit_points > 1) {
                    member.hit_points = std::max(1, member.hit_points - member.diseased);
                }
            }
        }

        // Nobody left standing: the party loses. Somebody drags the four
        // back to the last town — a week gone, the name dented, everyone
        // waking at a single hit point — every number the engine's own,
        // marked; the tables say nothing about defeat.
        {
            bool anyone = false;
            for (const auto& member : party) {
                anyone = anyone || member.hit_points > 0;
            }
            if (!anyone) {
                clock.advance_hours(7 * game::kHoursPerDay);
                reputation -= 10;
                for (auto& member : party) {
                    member.hit_points = 1;
                    member.poisoned = 0;
                    member.diseased = 0;
                    member.affliction.clear();
                }
                pick_up_message = "The party is dragged from the field. A week is lost.";
                pick_up_shown = SDL_GetTicks();
                if (!visited_towns.empty() && visited_towns.back() != session.file_name) {
                    if (open_map(visited_towns.back())) {
                        camera.position = {0, 32.0f * 30.0f, 0};
                        camera.yaw = 0.6f;
                        camera.pitch = -0.3f;
                    }
                }
            }
        }

        // The day turns: the cook makes food, the healer makes rounds, the
        // dawn casters cast — each at their row's own numbers — and every
        // seventh day the wages fall due, the cost column's own unit.
        if (clock.day() > last_hire_day) {
            last_hire_day = clock.day();
            for (const auto& h : hirelings) {
                if (h.benefit.food_per_day > 0 && party_food < h.benefit.food_cap) {
                    party_food = std::min(party_food + h.benefit.food_per_day,
                                          h.benefit.food_cap);
                }
                for (auto& member : party) {
                    const bool beyond = member.dead() || member.affliction == "Stone" ||
                                        member.affliction == "Eradicated";
                    if (h.benefit.heal_level >= 3 || (h.benefit.heal_level >= 1 && !beyond)) {
                        member.hit_points = member.max_hit_points;
                    }
                    if (h.benefit.heal_level >= 3 || (h.benefit.heal_level >= 2 && !beyond)) {
                        member.poisoned = 0;
                        member.diseased = 0;
                        member.affliction.clear();
                    }
                    if (h.benefit.bless_hours > 0) {
                        member.bless_until = std::max(
                            member.bless_until,
                            clock.minutes() + h.benefit.bless_hours * game::kMinutesPerHour);
                    }
                    if (h.benefit.heroism_hours > 0) {
                        member.heroism_until = std::max(
                            member.heroism_until,
                            clock.minutes() + h.benefit.heroism_hours * game::kMinutesPerHour);
                    }
                }
                // The Wind Master's daily Fly, at the row's written hours;
                // that it lifts at dawn is the engine's reading of "once
                // per day".
                if (h.benefit.fly_hours > 0) {
                    fly_until = std::max(
                        fly_until, clock.minutes() + h.benefit.fly_hours * game::kMinutesPerHour);
                }
            }
            if (!hirelings.empty() && clock.day() >= next_wage_day) {
                while (clock.day() >= next_wage_day) {
                    next_wage_day += 7;
                }
                int wages = 0;
                for (const auto& h : hirelings) {
                    wages += h.weekly_cost;
                }
                if (gold >= wages) {
                    gold -= wages;
                    pick_up_message = "You pay " + std::to_string(wages) + " gold in wages";
                } else {
                    hirelings.clear();
                    pick_up_message = "Unpaid, your followers leave";
                }
                pick_up_shown = SDL_GetTicks();
            }
        }

        // A fool's luck and an enchanter's wards lie on the party while they
        // are kept, reapplied as floors the way a fountain's blessing is.
        for (const auto& h : hirelings) {
            for (auto& member : party) {
                if (h.benefit.luck_bonus > 0) {
                    auto& luck = member.temp_attributes[static_cast<std::size_t>(
                        game::Attribute::Luck)];
                    luck = std::max(luck, h.benefit.luck_bonus);
                }
                if (h.benefit.elemental_protection > 0) {
                    for (const auto element :
                         {data::Resistance::Fire, data::Resistance::Electricity,
                          data::Resistance::Cold, data::Resistance::Poison}) {
                        auto& ward =
                            member.temp_resistances[static_cast<std::size_t>(element)];
                        ward = std::max(ward, h.benefit.elemental_protection);
                    }
                }
            }
        }

        // The map refills on its own interval, whether the party slept
        // through it or walked through it. A map that ships spawn points
        // rolls new groups at them — the day salts the seed, so each refill
        // is its own — and one that ships placed monsters stands them back
        // up.
        if (clock.day() >= next_refill) {
            if (!session.monster_spawns.empty()) {
                world::respawn_monsters(monster_stats, cache,
                                        static_cast<std::uint32_t>(session.map_id) * 2654435761U +
                                            static_cast<std::uint32_t>(clock.day()),
                                        session);
                mob.reset(session, monster_stats,
                          static_cast<std::uint32_t>(session.actors.size()) + 1u);
                battle.reset(session, monster_stats,
                             static_cast<std::uint32_t>(session.actors.size()) + 7u);
                shown_kind.assign(session.actors.size(), world::MonsterAnimation::Stand);
                shown_animation.assign(session.actors.size(), {});
            } else {
                battle.refill();
            }
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

        // Armour class is what the party is wearing plus its own footwork.
        for (std::size_t i = 0; i < party.size(); ++i) {
            // The worn enchantments, recomputed fresh: the standard rows'
            // own stats at their rolled strengths, the specials' parsed
            // prose.
            party[i].gear_attributes.fill(0);
            party[i].gear_resistances.fill(0);
            int gear_armor = 0;
            for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                if (party[i].equipped[slot] <= 0 || party[i].equipped_broken[slot]) {
                    continue;
                }
                const game::EnchantPower power =
                    power_of_enchant(party[i].worn_standard[slot],
                                     party[i].worn_strength[slot],
                                     party[i].worn_special[slot]);
                for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
                    party[i].gear_attributes[a] += power.attributes[a];
                }
                for (std::size_t r = 0; r < data::kResistanceCount; ++r) {
                    party[i].gear_resistances[r] += power.resistances[r];
                }
                gear_armor += power.armor_class;
            }
            // Worn skill on top of worn steel: each equipped piece whose
            // Skill Group's own line grants Armor Class adds the wearer's
            // points in it, the Squire's bonus included.
            int skilled_armor = 0;
            int squire = 0;
            for (const auto& h : hirelings) {
                squire = std::max(squire, h.benefit.armor_skill_bonus);
            }
            for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                const int worn = party[i].equipped[slot];
                if (worn <= 0 || party[i].equipped_broken[slot]) {
                    continue;
                }
                const auto* row = item_stats.at(static_cast<std::size_t>(worn));
                if (row == nullptr || row->skill_group.empty()) {
                    continue;
                }
                int points = squire;
                if (const auto it = party[i].skills.find(row->skill_group);
                    it != party[i].skills.end()) {
                    points += it->second;
                }
                skilled_armor += power_of_skill(row->skill_group, points).armor;
            }
            party[i].armor_class =
                game::attribute_bonus(party[i].attribute(game::Attribute::Speed)) +
                game::armour_of(party[i], item_stats) + party[i].temp_armor + skilled_armor +
                gear_armor;
        }

        if (want_rest) {
            const bool disturbed =
                battle.anything_near(session, camera.position, game::kRestDisturbance);
            // The camp's appetite, less the best food-saver hired, never
            // below the one day the professions' own floors state.
            int saved = 0;
            for (const auto& h : hirelings) {
                saved = std::max(saved, h.benefit.food_saved_camping);
            }
            const int cost = std::max(1, game::kRestFoodCost - saved);
            const game::RestResult result =
                game::rest(party, clock, disturbed, party_food, cost);
            if (result == game::RestResult::Rested) {
                // What lasts until a rest ends with one, fountains included.
                for (auto& member : party) {
                    member.rest_expires();
                }
                for (int a = 25; a <= 31; ++a) {
                    script_state.variables[a] = 0;
                }
            }
            pick_up_message = game::rest_message(result, clock);
            pick_up_shown = SDL_GetTicks();
        }

        // Doors, signs and switches: using one walks its event, and the
        // event's own checks decide what happens — a gated exit stays shut
        // until the quest bit it asks for is set. See
        // docs/formats/map-events.md.
        game::AimedFace aimed = game::aimed_face(session, camera.position, camera.forward());
        // `--walk N` uses event N as though the party had, once, on startup.
        if (walk_on_start >= 0) {
            aimed.event_id = static_cast<std::uint16_t>(walk_on_start);
            aimed.distance = 0.0f;
            want_strike = true;
            walk_on_start = -1;
        }
        if (want_strike && aimed.found()) {
            // The walker's view of the purse and the packs.
            script_state.gold = gold;
            script_state.food = party_food;
            script_state.items.clear();
            for (const auto& pack : packs) {
                for (const auto& carried : pack.items()) {
                    script_state.items.push_back(carried.item_id);
                }
            }
            const bool local = session.script.defines(aimed.event_id);
            const game::WalkOutcome outcome = game::walk_event(
                local ? session.script : global_script, aimed.event_id, script_state, walk_from);
            walk_from = -1;
            gold = script_state.gold;
            const std::string rewards = reward_note(outcome);

            // What it said, resolved before any travel drops these strings.
            // The map's own events speak through its `.STR`; the global
            // script's speak through `npctext.txt` — the letter quest's two
            // branches say exactly rows 1 and 3 of it.
            std::string said_text;
            for (const int index : outcome.said) {
                std::string text;
                if (local) {
                    if (index >= 0 &&
                        static_cast<std::size_t>(index) < session.script_strings.size()) {
                        text = data::cp1252_to_utf8(std::string(
                            session.script_strings.at(static_cast<std::size_t>(index))));
                    }
                } else if (const auto* entry = dialogue.at(index); entry != nullptr) {
                    text = data::cp1252_to_utf8(entry->text);
                }
                if (!text.empty()) {
                    said_text += (said_text.empty() ? "" : "  ") + text;
                }
            }

            // What it handed over, and what it asked for.
            for (const int id : outcome.given) {
                if (const std::string name = give_item(id); !name.empty()) {
                    said_text += (said_text.empty() ? "You receive " : "  You receive ") + name;
                }
            }
            for (const int id : outcome.taken) {
                take_item(id);
            }

            // A thrown switch is drawn thrown: the event names a face and
            // the texture it now wears. Indoors the id reads as an index
            // into the map's own faces; the few outdoor uses are not
            // applied yet. `inferred`
            for (const auto& [face, texture] : outcome.retextures) {
                if (session.indoor() && face < session.blv.faces.size()) {
                    session.blv.faces[face].texture_name = texture;
                }
            }

            // A thrown door moves: its own vertices slide along its own
            // direction by its own distance, and the collision world is
            // rebuilt so the doorway really opens. The move is a snap
            // rather than the original's timed slide. `inferred`
            bool doors_moved = false;
            for (const auto& [id, state] : outcome.doors) {
                for (auto& door : session.doors) {
                    if (door.id != static_cast<std::uint32_t>(id)) {
                        continue;
                    }
                    door.open = state == 2 ? !door.open : state != 0;
                    move_door(door);
                    doors_moved = true;
                    break;
                }
            }
            // A summon fills the room: the count it asks, from the map's
            // own encounter slot, in the A/B/C variant it names, spread
            // around the point the way spawn groups are.
            bool summoned = false;
            for (const auto& called : outcome.summons) {
                if (called.slot < 1 || called.slot > 3) {
                    continue;
                }
                const auto& slot_data =
                    session.encounters[static_cast<std::size_t>(called.slot - 1)];
                const int base = world::encounter_monster_id(monster_stats, slot_data);
                if (base <= 0) {
                    continue;
                }
                const int monster_id = base + std::clamp(called.variant, 1, 3) - 1;
                for (int i = 0; i < std::max(1, called.count); ++i) {
                    const auto [dx, dy] = world::spawn_offset(i, std::max(1, called.count));
                    const render::Vec3 at = world::to_render_space(
                        called.x + static_cast<int>(dx), called.y + static_cast<int>(dy),
                        called.z);
                    summoned = world::summon_actor(monster_stats, cache, monster_id, at,
                                                   session) ||
                               summoned;
                }
            }
            if (summoned) {
                battle.recruit(session, monster_stats);
                mob.recruit(session, monster_stats);
                shown_kind.resize(session.actors.size(), world::MonsterAnimation::Stand);
                shown_animation.resize(session.actors.size());
            }
            // A launch puts its sprite in the air; an aimless one flies at
            // the party, which is this engine's reading of a trap.
            if (!outcome.launches.empty()) {
                auto started = game::start_launches(outcome.launches, session.sprite_frames,
                                                    camera.position);
                launches.insert(launches.end(), started.begin(), started.end());
            }
            // A question stops the walk and waits at the message line.
            if (outcome.ask && local) {
                ask_event = aimed.event_id;
                ask_pending = *outcome.ask;
                ask_typed.clear();
            }

            if (doors_moved) {
                world::rebuild_indoor_collision(session);
                // No script opcode names event sounds — a sweep of every
                // unnamed opcode's arguments against the sound table found
                // none — so the working of a door is this engine's choice
                // from the archive's own names. `inferred`
                ambient.play_once("stone door0101");
            } else if (!outcome.retextures.empty()) {
                ambient.play_once("WoodDRClose");
            }

            // A door into an establishment opens its counter.
            if (outcome.building != 0) {
                for (std::size_t i = 0; i < shops_here.size(); ++i) {
                    if (static_cast<std::uint32_t>(shops_here[i]->id) != outcome.building) {
                        continue;
                    }
                    if (!clock.open(shops_here[i]->opens, shops_here[i]->closes)) {
                        pick_up_message = data::cp1252_to_utf8(shops_here[i]->name) +
                                          " is closed; open " +
                                          std::to_string(shops_here[i]->opens) + " to " +
                                          std::to_string(shops_here[i]->closes);
                        pick_up_shown = SDL_GetTicks();
                        break;
                    }
                    open_shop = static_cast<int>(i);
                    shop_stock =
                        stock_for(*shops_here[static_cast<std::size_t>(open_shop)], outcome.building * 2654435761U);
                    shop_said.clear();
                    break;
                }
            }
            // A chest gives up what the map's treasure level rolls — after
            // any trap the map's own difficulty put on it has its say.
            if (outcome.chest >= 0 && !opened_chests.contains(outcome.chest)) {
                // A locked chest — the Lock column's own chance — stays
                // shut until the party's best Disarm reaches the number.
                int party_disarm = 0;
                for (const auto& member : party) {
                    if (const auto it = member.skills.find("Disarm Traps");
                        it != member.skills.end()) {
                        party_disarm = std::max(party_disarm, it->second);
                    }
                }
                for (const auto& h : hirelings) {
                    party_disarm = std::max(party_disarm, h.benefit.disarm_bonus);
                }
                const bool locked_fast =
                    game::chest_locked(session.lock_difficulty, outcome.chest) &&
                    !game::disarmed(session.lock_difficulty, party_disarm);
                if (locked_fast) {
                    said_text = "The chest is locked fast.";
                }
                if (!locked_fast) {
                opened_chests.insert(outcome.chest);
                std::string took;
                if (game::chest_trapped(session.trap_difficulty, outcome.chest)) {
                    int disarm = 0, perception = 0;
                    for (const auto& member : party) {
                        if (const auto it = member.skills.find("Disarm Traps");
                            it != member.skills.end()) {
                            disarm = std::max(disarm, it->second);
                        }
                        if (const auto it = member.skills.find("Perception");
                            it != member.skills.end()) {
                            perception = std::max(perception, it->second);
                        }
                    }
                    for (const auto& h : hirelings) {
                        disarm = std::max(disarm, h.benefit.disarm_bonus);
                        perception = std::max(perception, h.benefit.perception_bonus);
                    }
                    if (game::disarmed(session.trap_difficulty, disarm)) {
                        took = "A trap clicks, disarmed.  ";
                    } else {
                        const game::TrapBlast blast = game::spring_trap(
                            session.trap_difficulty, party, perception, misc_random);
                        int hurt = 0;
                        for (std::size_t i = 0; i < party.size(); ++i) {
                            if (blast.damage[i] <= 0) {
                                continue;
                            }
                            party[i].hit_points =
                                std::max(0, party[i].hit_points - blast.damage[i]);
                            ++hurt;
                        }
                        took = hurt == 0 ? "A trap fires wide.  "
                                         : "The chest was trapped!  ";
                    }
                }
                for (const auto& rolled : game::chest_contents(
                         static_cast<std::size_t>(session.treasure_level), random_items, item_stats,
                         standard_bonuses, special_bonuses,
                         static_cast<std::uint32_t>(outcome.chest + 1) * 40503U,
                         game::kChestItems)) {
                    const int id = rolled.item_id;
                    const auto* row = item_stats.at(static_cast<std::size_t>(id));
                    if (row == nullptr) {
                        continue;
                    }
                    const render::Texture& icon = cache.icon(row->picture);
                    const int w = std::max(1, game::cells_across(static_cast<int>(icon.width())));
                    const int h = std::max(1, game::cells_across(static_cast<int>(icon.height())));
                    const bool known = arrives_identified(*row);
                    for (auto& pack : packs) {
                        if (pack.add(id, w, h, known, rolled.standard_bonus,
                                     rolled.standard_bonus_strength, rolled.special_bonus,
                                     rolled.charges)) {
                            took += (took.empty() || took.back() == ' ' ? "You find "
                                                                          : ", ") +
                                    data::cp1252_to_utf8(known ||
                                                                 row->unidentified_name.empty()
                                                             ? row->name
                                                             : row->unidentified_name);
                            break;
                        }
                    }
                }
                said_text = took.empty() ? "The chest is empty" : took;
                }
            }

            // A fountain's blessing: the walker keeps the seven attributes'
            // temporary bonuses in variables 25..31 — Might named by the
            // fountain's own words, the rest by the prose join, which puts
            // Speed at 29 and Accuracy at 30, the reverse of the sheet's
            // order — and they lie on the whole party until a rest.
            // `inferred` for the party-wide reach and the until.
            static constexpr std::size_t kTempOrder[7] = {0, 1, 2, 3, 5, 4, 6};
            for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
                const int bonus = script_state.variables[25 + static_cast<int>(a)];
                if (bonus <= 0) {
                    continue;
                }
                for (auto& member : party) {
                    member.temp_attributes[kTempOrder[a]] =
                        std::max(member.temp_attributes[kTempOrder[a]], bonus);
                }
            }

            if (!rewards.empty()) {
                said_text += (said_text.empty() ? "" : "  ") + rewards;
            }
            if (!said_text.empty()) {
                pick_up_message = said_text;
                pick_up_shown = SDL_GetTicks();
            }

            // Travel last: it replaces the session everything above read.
            if (outcome.travel) {
                const auto& travel = *outcome.travel;
                const bool stays = travel.destination.empty();
                if (stays || open_map(travel.destination)) {
                    camera.position = world::to_render_space(travel.x, travel.y, travel.z);
                    camera.position.y += game::kEyeHeight;
                    // The facing counts 0..2047 anticlockwise from MM6's +X;
                    // the camera's yaw looks down -Z at zero. `inferred`
                    camera.yaw =
                        (static_cast<float>(travel.facing) / 2048.0f) * 2.0f * render::kPi +
                        render::kPi / 2.0f;
                    camera.pitch = 0.0f;
                    pick_up_message = stays ? "You step through" : "You travel to " + session.title();
                    pick_up_shown = SDL_GetTicks();
                }
            }
            if (outcome.acted()) {
                want_strike = false;  // using a door is not swinging at it
            }
        }

        // A blow lands on whatever the party is aiming at, in reach, alive.
        // A drawn bow — the Missile equip type, the item table's own word —
        // reaches to missile range; everyone else needs arm's length.
        const auto reach_of = [&](const game::Character& who) {
            const int held = who.equipped[static_cast<std::size_t>(game::Slot::Weapon)];
            const auto* row = held > 0 ? item_stats.at(static_cast<std::size_t>(held)) : nullptr;
            return row != nullptr && row->equip_type == data::ItemEquipType::Missile
                       ? game::kMissileRange
                       : game::kPartyReach;
        };
        if (want_strike && party_recovery <= 0.0f) {
            const render::Vec3 forward = camera.forward();
            float longest = game::kPartyReach;
            for (const auto& member : party) {
                if (member.can_act() && !member.afraid()) {
                    longest = std::max(longest, reach_of(member));
                }
            }
            if (const std::size_t target =
                    game::aimed_actor(session, battle, camera.position, forward, longest);
                target != game::kNoActor) {
                const float dx = session.actors[target].position.x - camera.position.x;
                const float dz = session.actors[target].position.z - camera.position.z;
                const float away = std::sqrt(dx * dx + dz * dz);
                for (int tries = 0; tries < 4; ++tries) {
                    const auto who = static_cast<std::size_t>(striker);
                    striker = (striker + 1) % static_cast<int>(party.size());
                    // The unconscious and the held-under act for nobody, and
                    // the afraid keep their feet but lose their swing.
                    if (!party[who].can_act() || party[who].afraid() ||
                        reach_of(party[who]) < away) {
                        continue;
                    }
                    // The held weapon's special rider, if its prose rolls.
                    game::EnchantPower rider;
                    if (const int sp = party[who].worn_special[static_cast<std::size_t>(
                            game::Slot::Weapon)];
                        sp > 0) {
                        if (const auto* bonus =
                                special_bonuses.at(static_cast<std::size_t>(sp))) {
                            rider = game::special_power(*bonus);
                        }
                    }
                    std::string blow =
                        battle.strike(target, party[who], packs[who], session, monster_stats,
                                      item_stats, random_items, standard_bonuses, special_bonuses,
                                      weapon_skill_of(party[who]), rider.extra_damage,
                                      rider.damage_element);
                    if (!blow.empty()) {
                        // The release and the blow: archive names picked by
                        // this engine, marked as such.
                        const int held_now =
                            party[who].equipped[static_cast<std::size_t>(game::Slot::Weapon)];
                        const auto* held_row =
                            held_now > 0 ? item_stats.at(static_cast<std::size_t>(held_now))
                                         : nullptr;
                        ambient.play_once(held_row != nullptr &&
                                                  held_row->equip_type ==
                                                      data::ItemEquipType::Missile
                                              ? "ArchShoot"
                                              : "hit with sword 02m");
                        pick_up_message = std::move(blow);
                        pick_up_shown = SDL_GetTicks();
                        pending_round = turn_based;
                        // "Skill reduces recovery time", at the engine's own
                        // percent a point — and worn armor's own penalty on
                        // top, reduced or eliminated by the armor skill's
                        // higher lines.
                        float armor_drag = 0.0f;
                        const int worn =
                            party[who].equipped[static_cast<std::size_t>(game::Slot::Armor)];
                        if (const auto* armor_row =
                                worn > 0 ? item_stats.at(static_cast<std::size_t>(worn))
                                         : nullptr;
                            armor_row != nullptr && !armor_row->skill_group.empty()) {
                            armor_drag = game::armor_penalty(armor_row->skill_group);
                            int points = 0;
                            if (const auto it = party[who].skills.find(armor_row->skill_group);
                                it != party[who].skills.end()) {
                                points = it->second;
                            }
                            if (const auto* skill = skill_table.find(armor_row->skill_group);
                                skill != nullptr && points > 0) {
                                const int lift =
                                    game::skill_power(skill->text, points).armor_penalty_lift;
                                armor_drag = lift >= 2 ? 0.0f
                                             : lift == 1 ? armor_drag / 2.0f
                                                         : armor_drag;
                            }
                        }
                        party_recovery = game::kPartyRecovery *
                                         weapon_skill_of(party[who]).recovery_scale *
                                         (1.0f + armor_drag);
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
        std::vector<game::ActiveLaunch> in_flight = launches;
        for (const auto& shot : spell_shots) {
            in_flight.push_back(shot.flight);
        }
        for (const auto& burst : spell_bursts) {
            game::ActiveLaunch flash;
            flash.animation = burst.animation;
            flash.position = burst.position;
            in_flight.push_back(std::move(flash));
        }
        draw_billboards(scene, session, cache, game::sprite_ticks(SDL_GetTicks()), mob,
                        camera.position, shown_animation, in_flight);
        if (show_boxes && session.outdoor()) {
            draw_boxes(scene, session);
        }

        if (show_labels) {
            draw_labels(scene, session, font, camera.position);
        }
        if (show_directory) {
            draw_directory(scene, font, session, clock, trade_talk);
        }
        for (std::size_t i = 0; i < party.size(); ++i) {
            if (party[i].hit_points < known_hp[i]) {
                wince_until[i] = SDL_GetTicks() + 500;
            }
            known_hp[i] = party[i].hit_points;
        }
        if (shown_member < 0 && shown_pack < 0 && open_shop < 0 && !creating && !show_journal) {
            std::array<bool, 4> wincing{};
            for (std::size_t i = 0; i < party.size(); ++i) {
                wincing[i] = SDL_GetTicks() < wince_until[i];
            }
            draw_frame(scene, cache);
            draw_party_strip(scene, cache, party, wincing);
            // The engine's own readouts keep to the corner no shipped
            // piece claims.
            int corner_y = 344;
            const int corner_line = font.height() + 2;
            // The weekday gets its own line; the corner is 172 wide.
            game::draw_text(scene.framebuffer(), font, 474, corner_y,
                            "day " + std::to_string(clock.day() + 1) + ", " +
                                std::string(clock.weekday()),
                            render::Color{210, 205, 185, 255}, render::Color{0, 0, 0, 255});
            corner_y += corner_line;
            game::draw_text(scene.framebuffer(), font, 474, corner_y, clock.hhmm(),
                            render::Color{210, 205, 185, 255}, render::Color{0, 0, 0, 255});
            corner_y += corner_line;
            const std::string purse =
                std::to_string(gold) + " gold, " + std::to_string(party_food) + " food" +
                (turn_based ? "  \x95 turn-based" : "");
            game::draw_text(scene.framebuffer(), font, 474, corner_y, purse,
                            render::Color{180, 175, 155, 255}, render::Color{0, 0, 0, 255});
            corner_y += corner_line;
            for (const auto& h : hirelings) {
                game::draw_text(scene.framebuffer(), font, 474, corner_y,
                                "+ " + h.name + ", " + h.profession,
                                render::Color{190, 190, 215, 255}, render::Color{0, 0, 0, 255});
                corner_y += corner_line;
            }
            if (ask_event >= 0) {
                const int prompt = ask_pending.prompt;
                std::string line =
                    prompt >= 0 &&
                            static_cast<std::size_t>(prompt) < session.script_strings.size()
                        ? data::cp1252_to_utf8(std::string(session.script_strings.at(
                              static_cast<std::size_t>(prompt))))
                        : std::string("Answer?");
                line += " " + ask_typed + "_";
                game::draw_text(scene.framebuffer(), font, 8, 24, line,
                                render::Color{235, 225, 170, 255}, render::Color{0, 0, 0, 255});
            }
            if (!pick_up_message.empty() && SDL_GetTicks() - pick_up_shown < 3000) {
                // The footer is the game's message strip; the line lives
                // there now.
                game::draw_text(scene.framebuffer(), font, 12, kHeight - 17, pick_up_message,
                                render::Color{235, 225, 170, 255}, render::Color{0, 0, 0, 255});
            }
        }
        if (shown_member >= 0) {
            draw_sheet(scene, font, cache, party[static_cast<std::size_t>(shown_member)],
                       stat_descriptions, class_descriptions, clock.minutes(), award_texts,
                       script_state.awards);
        }
        if (talking_to >= 0 && open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
            if (talking_to < static_cast<int>(here.size())) {
                const auto person = patched(here[static_cast<std::size_t>(talking_to)]);
                draw_conversation(scene, font,
                                  game::talk_to(person, dialogue, personalities, trade_talk,
                                                clock, interface_words, party[0].name,
                                                game::face_is_female(party[0].face),
                                                standing_for(person.npc_id)),
                                  talk_answer);
            }
        } else if (open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
            if (game::is_temple(shop)) {
                draw_temple(scene, font, shop, party, gold, shop_said);
            } else if (game::is_bank(shop)) {
                draw_bank(scene, font, shop, gold, bank_gold, shop_said);
            } else if (game::is_training(shop)) {
                draw_training(scene, font, shop, party, gold, shop_said);
            } else if (game::is_travel(shop)) {
                draw_travel(scene, font, shop, game::routes_of(shop, map_stats),
                            game::fare_of(shop), clock, gold, shop_said);
            } else {
                draw_shop(scene, font, shop, shop_stock, item_stats, merchant_words, gold,
                          shop_said);
            }
        }
        if (shown_pack >= 0) {
            const auto who = static_cast<std::size_t>(shown_pack);
            int sale_offer = -1;
            if (open_shop >= 0) {
                if (const auto* under = packs[who].at(pack_cursor_x, pack_cursor_y);
                    under != nullptr) {
                    if (const auto* row =
                            item_stats.at(static_cast<std::size_t>(under->item_id));
                        row != nullptr) {
                        const int offer = game::offer_price(*row);
                        const int sweet = row->value - offer;
                        sale_offer = std::min(
                            row->value,
                            offer + sweet * (row->value - haggled(row->value)) /
                                        std::max(1, row->value));
                    }
                }
            }
            draw_pack(scene, font, cache, party[who], packs[who], item_stats, standard_bonuses,
                      special_bonuses, pack_cursor_x, pack_cursor_y, sale_offer);
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
        // A face with a script on it names itself, which the inspect panel
        // shows the same way it names a monster. An establishment's door
        // names the establishment, from the design table's own row.
        if (aimed.found() && shown_member < 0 && shown_pack < 0 && open_shop < 0) {
            std::string named;
            if (const std::uint32_t row = session.script.building_of(aimed.event_id); row != 0) {
                for (const auto* shop : shops_here) {
                    if (static_cast<std::uint32_t>(shop->id) == row) {
                        named = data::cp1252_to_utf8(shop->name);
                        break;
                    }
                }
            }
            if (named.empty()) {
                named = game::face_name(session, aimed.event_id);
            }
            if (!named.empty()) {
                game::draw_text(scene.framebuffer(), font, kWidth / 2 - font.text_width(named) / 2,
                                kHeight / 2 + 16, named, render::Color{225, 220, 190, 255},
                                render::Color{0, 0, 0, 255});
            }
        }
        draw_panel(scene, font,
                   game::inspect(session, monster_stats, item_stats, spell_stats, camera.position,
                                 camera.forward(), visible));

        // Wizard Eye's automap, "in the upper right corner ... while
        // outdoors": dots in a north-up window. The window's reach and the
        // dot colours are the engine's; what each rank shows is the cells'
        // own — monsters, then treasure, then points of interest. A hired
        // Cartographer keeps it lit at expert, as their row says.
        const bool eye_open =
            session.outdoor() &&
            (clock.minutes() < eye_until ||
             std::any_of(hirelings.begin(), hirelings.end(),
                         [](const auto& h) { return h.benefit.wizard_eye; }));
        if (eye_open) {
            const int rank = std::any_of(hirelings.begin(), hirelings.end(),
                                         [](const auto& h) { return h.benefit.wizard_eye; })
                                 ? std::max(eye_rank, 1)
                                 : eye_rank;
            constexpr int kBox = 110;
            constexpr float kReach = 6144.0f;  // world units across the box
            const int left = kWidth - kBox - 8;
            const int top = 30;
            auto pixels = scene.framebuffer().color();
            for (int y = top; y < top + kBox; ++y) {
                for (int x = left; x < left + kBox; ++x) {
                    const auto i =
                        (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
                    pixels[i] = static_cast<std::uint8_t>(pixels[i] / 3 + 10);
                    pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 3 + 10);
                    pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 3 + 20);
                }
            }
            for (int d = 0; d < kBox; ++d) {
                for (const auto [ex, ey] :
                     {std::pair{left + d, top}, {left + d, top + kBox - 1}, {left, top + d},
                      {left + kBox - 1, top + d}}) {
                    const auto i =
                        (static_cast<std::size_t>(ey) * kWidth + static_cast<std::size_t>(ex)) * 4;
                    pixels[i] = 180;
                    pixels[i + 1] = 175;
                    pixels[i + 2] = 140;
                }
            }
            const auto plot = [&](const render::Vec3& at, render::Color colour) {
                const float dx = (at.x - camera.position.x) / kReach + 0.5f;
                const float dz = (at.z - camera.position.z) / kReach + 0.5f;
                if (dx < 0.0f || dx >= 1.0f || dz < 0.0f || dz >= 1.0f) {
                    return;
                }
                const int px = left + static_cast<int>(dx * kBox);
                const int py = top + kBox - 1 - static_cast<int>(dz * kBox);
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int qx = px + ox;
                        const int qy = py + oy;
                        if (qx < left || qx >= left + kBox || qy < top || qy >= top + kBox) {
                            continue;
                        }
                        const auto i = (static_cast<std::size_t>(qy) * kWidth +
                                        static_cast<std::size_t>(qx)) *
                                       4;
                        pixels[i] = colour.r;
                        pixels[i + 1] = colour.g;
                        pixels[i + 2] = colour.b;
                    }
                }
            };
            for (std::size_t i = 0; i < session.actors.size(); ++i) {
                if (battle.alive(i)) {
                    plot(session.actors[i].position, {220, 60, 60, 255});
                }
            }
            if (rank >= 1) {
                for (const auto& object : session.objects) {
                    plot(object.position, {230, 210, 90, 255});
                }
            }
            if (rank >= 2) {
                for (const auto& mesh : session.meshes) {
                    for (const auto& facet : mesh.facets) {
                        if (facet.event_id == 0 || facet.vertex_count < 1 ||
                            facet.vertex_ids[0] >= mesh.vertices.size()) {
                            continue;
                        }
                        const auto& v = mesh.vertices[facet.vertex_ids[0]];
                        plot(world::to_render_space(v.x, v.y, v.z), {110, 200, 220, 255});
                    }
                }
            }
            plot(camera.position, {240, 240, 240, 255});
        }
        if (beaconing && font.glyph_count() > 0) {
            const int line = font.height() + 2;
            int y = kHeight / 2 - (static_cast<int>(beacons.size()) + 2) * line / 2;
            game::draw_text(scene.framebuffer(), font, kWidth / 2 - 90, y - line,
                            "The beacons stand at:", render::Color{225, 220, 190, 255},
                            render::Color{0, 0, 0, 255});
            std::size_t shown = 0;
            for (; shown < beacons.size() && shown < 8; ++shown) {
                std::string label = beacons[shown].map;
                for (const auto& m : map_stats.entries()) {
                    if (m.file_name == beacons[shown].map && !m.name.empty()) {
                        label = m.name;
                        break;
                    }
                }
                game::draw_text(scene.framebuffer(), font, kWidth / 2 - 90, y,
                                std::to_string(shown + 1) + "  " + data::cp1252_to_utf8(label),
                                render::Color{230, 230, 230, 255},
                                render::Color{0, 0, 0, 255});
                y += line;
            }
            if (static_cast<int>(beacons.size()) < beacon_capacity) {
                game::draw_text(scene.framebuffer(), font, kWidth / 2 - 90, y,
                                std::to_string(shown + 1) + "  set a new marker here",
                                render::Color{190, 215, 190, 255},
                                render::Color{0, 0, 0, 255});
            }
        }
        if (street_talk >= 0 && font.glyph_count() > 0) {
            world::SessionNpc passerby;
            passerby.name = street_name;
            passerby.npc_id = 100000 + street_talk;
            passerby.profession = "Peasant";
            passerby.personality = "Peasant";
            draw_conversation(scene, font,
                              game::talk_to(passerby, dialogue, personalities, trade_talk,
                                            clock, interface_words, party[0].name,
                                            game::face_is_female(party[0].face),
                                            standing_for(passerby.npc_id)),
                              talk_answer);
        }
        if (porting && font.glyph_count() > 0) {
            const int line = font.height() + 2;
            int y = kHeight / 2 - static_cast<int>(visited_towns.size()) * line / 2;
            game::draw_text(scene.framebuffer(), font, kWidth / 2 - 80, y - line * 2,
                            "The portal reaches:", render::Color{225, 220, 190, 255},
                            render::Color{0, 0, 0, 255});
            for (std::size_t i = 0; i < visited_towns.size() && i < 9; ++i) {
                std::string label = visited_towns[i];
                for (const auto& m : map_stats.entries()) {
                    if (m.file_name == visited_towns[i] && !m.name.empty()) {
                        label = m.name;
                        break;
                    }
                }
                game::draw_text(scene.framebuffer(), font, kWidth / 2 - 80, y,
                                std::to_string(i + 1) + "  " + data::cp1252_to_utf8(label),
                                render::Color{230, 230, 230, 255},
                                render::Color{0, 0, 0, 255});
                y += line;
            }
        }
        if (show_journal && shown_member < 0 && shown_pack < 0 && open_shop < 0) {
            draw_journal(scene, font, quest_texts, award_texts, script_state.bits,
                         script_state.awards);
        }
        if (creating) {
            draw_creation(scene, font, cache, party, create_slot, stat_descriptions,
                          class_descriptions);
        }

        // The map's name, drawn with the game's own font, inside the
        // viewport's frame rather than across it.
        if (font.glyph_count() > 0 && !creating && !show_journal) {
            game::draw_text(scene.framebuffer(), font, 12, 12, session.title(),
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
