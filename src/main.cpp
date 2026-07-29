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
#include "game/inspect.hpp"
#include "game/inventory.hpp"
#include "game/monster_ai.hpp"
#include "game/music_player.hpp"
#include "game/party.hpp"
#include "game/player.hpp"
#include "game/rest.hpp"
#include "game/save.hpp"
#include "game/script_walk.hpp"
#include "game/shop.hpp"
#include "game/sprites.hpp"
#include "game/temple.hpp"
#include "game/text.hpp"
#include "game/training.hpp"
#include "game/travel.hpp"

namespace {

using namespace starhaven;

// Where a save lands: beside where the engine was run, in this
// engine's own text format.
constexpr const char* kSaveFile = "starhaven.save";

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
              << "  F5/F9      save the game / load it back\n"
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
              << "  --shop N            open the Nth establishment's counter\n"
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
                const data::DescriptionTable& classes, std::int64_t minute) {
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
        } else if (who.poisoned > 0) {
            text += "  poisoned";
        } else if (who.diseased > 0) {
            text += "  diseased";
        } else if (!who.affliction.empty()) {
            text += "  " + who.affliction;
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
               const game::Character& who, const game::Pack& pack,  // NOLINT
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
                    "1-4 choose a character, E wear, U drink, M mix, I closes", dim, shadow);
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
        game::draw_text(scene.framebuffer(), font, 24, kHeight - line * 2 - 8, how + " them", dim,
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                    "1-3 ask about something, T closes", dim, shadow);
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
    int bank_gold = 0;  // what the vault keeps; no table pays interest
    int open_shop = -1;  // an index into shops_here, or none
    std::vector<game::StockItem> shop_stock;
    std::string shop_said;
    std::set<int> opened_chests;  // a chest gives up its contents once

    std::int64_t last_poison_hour = 0;  // when poison last gnawed

    // A die for what is neither combat's nor a map's: potion explosions.
    Mm6Random misc_random{0xA1C4E317u};

    // The event walker's memory: quest bits and event variables, which are
    // the party's rather than any map's, so they survive travelling.
    game::WalkState script_state;

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
    std::string talk_answer;

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
    if (start_shop >= 1 && start_shop <= static_cast<int>(shops_here.size())) {
        open_shop = start_shop - 1;
        const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
        shop_stock =
            game::stock_of(shop, random_items, item_stats, standard_bonuses, special_bonuses,
                           static_cast<std::uint32_t>(shop.id) * 2654435761U);
    }

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
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_E &&
                       shown_pack >= 0) {
                // Wear the first thing in this pack that can be worn.
                auto& who = party[static_cast<std::size_t>(shown_pack)];
                auto& pack = packs[static_cast<std::size_t>(shown_pack)];
                for (const auto& carried : pack.items()) {
                    const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                    if (row == nullptr) {
                        continue;
                    }
                    const game::Slot slot = game::slot_for(row->equip_type);
                    if (slot == game::Slot::Count) {
                        continue;
                    }
                    // What was there comes off and goes back in the pack.
                    const int worn = who.equipped[static_cast<std::size_t>(slot)];
                    who.equipped[static_cast<std::size_t>(slot)] = carried.item_id;
                    pack.remove(carried.x, carried.y);
                    if (worn > 0) {
                        const auto* old = item_stats.at(static_cast<std::size_t>(worn));
                        const render::Texture& icon =
                            old == nullptr ? cache.icon("") : cache.icon(old->picture);
                        (void)pack.add(
                            worn, std::max(1, game::cells_across(static_cast<int>(icon.width()))),
                            std::max(1, game::cells_across(static_cast<int>(icon.height()))));
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
                std::ofstream file(kSaveFile);
                file << game::save_text(state);
                pick_up_message = file.good() ? "Saved" : "Could not write the save";
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F9) {
                // Load: back to the saved map, standing where the party stood.
                game::SaveState state;
                std::ifstream file(kSaveFile);
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
                    script_state.bits = state.bits;
                    script_state.variables = state.variables;
                    script_state.npc_topics = state.npc_topics;
                    script_state.npc_places = state.npc_places;
                    party = state.party;
                    for (std::size_t i = 0; i < packs.size(); ++i) {
                        packs[i].clear();
                        for (const auto& item : state.packs[i]) {
                            (void)packs[i].place(item.item_id, item.x, item.y, item.width,
                                                 item.height);
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
                for (const auto& carried : pack.items()) {
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
                for (const auto& carried : pack.items()) {
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
                       open_shop >= 0) {
                // Talk to whoever the NPC table puts in this establishment.
                talking_to = talking_to >= 0 ? -1 : 0;
                talk_answer.clear();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_B &&
                       open_shop >= 0) {
                open_shop = -1;
                shop_said.clear();
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_F) {
                // Mend what monsters broke: half the item's value per piece,
                // said with the merchant table's own Repair line. The price
                // is this engine's. `inferred`
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
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       event.key.key == SDLK_S) {
                // Sell the first thing the first character is carrying.
                for (auto& pack : packs) {
                    if (pack.empty()) {
                        continue;
                    }
                    const auto carried = pack.items().front();
                    const auto* row = item_stats.at(static_cast<std::size_t>(carried.item_id));
                    if (row == nullptr) {
                        break;
                    }
                    gold += game::offer_price(*row);
                    pack.remove(carried.x, carried.y);
                    shop_said = std::string(
                        game::merchant_line(merchant_words, data::MerchantAction::Sell, true));
                    break;
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
                            script_state.items.clear();
                            for (const auto& pack : packs) {
                                for (const auto& carried : pack.items()) {
                                    script_state.items.push_back(carried.item_id);
                                }
                            }
                            const game::WalkOutcome outcome = game::walk_event(
                                global_script, static_cast<std::uint16_t>(id), script_state);
                            gold = script_state.gold;
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
                        if (!needs) {
                            shop_said = who.name + " needs no healing.";
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
                            clock.advance_hours(route.days * game::kHoursPerDay);
                            if (open_map(route.map_file)) {
                                camera.position = {0, 32.0f * 30.0f, 0};
                                camera.yaw = 0.6f;
                                camera.pitch = -0.3f;
                                pick_up_message = "After " + std::to_string(route.days) +
                                                  (route.days == 1 ? " day" : " days") +
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
                        const bool affordable = row != nullptr && offered.price <= gold;
                        bool carried = false;
                        if (affordable) {
                            const render::Texture& icon = cache.icon(row->picture);
                            const int w =
                                std::max(1, game::cells_across(static_cast<int>(icon.width())));
                            const int h =
                                std::max(1, game::cells_across(static_cast<int>(icon.height())));
                            for (auto& pack : packs) {
                                if (pack.add(offered.item_id, w, h)) {
                                    carried = true;
                                    break;
                                }
                            }
                        }
                        if (carried) {
                            gold -= offered.price;
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
                        counter.offered = offered.price;
                        shop_said = game::substitute(game::merchant_line(merchant_words,
                                                                         data::MerchantAction::Buy,
                                                                         affordable),
                                                     counter, interface_words);
                    }
                } else if (show_directory && chosen < static_cast<int>(shops_here.size())) {
                    open_shop = chosen;
                    show_directory = false;
                    const auto& shop = *shops_here[static_cast<std::size_t>(open_shop)];
                    shop_stock = game::stock_of(shop, random_items, item_stats, standard_bonuses,
                                                special_bonuses,
                                                static_cast<std::uint32_t>(shop.id) * 2654435761U);
                    shop_said.clear();
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
                for (std::size_t who = 0; who < packs.size() && !read; ++who) {
                    if (party[who].hit_points <= 0) {
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
                                                party[who].level, spell->element,
                                                party[who].name, session, monster_stats,
                                                item_stats, random_items, standard_bonuses,
                                                special_bonuses);
                        } else if (apply_buff(*spell, party[who], party[who].level)) {
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " +
                                   data::cp1252_to_utf8(spell->name) +
                                   " for its written time";
                        } else {
                            continue;  // a spell this slice cannot cast yet
                        }
                        packs[who].remove(carried.x, carried.y);
                        pick_up_message = std::move(what);
                        pick_up_shown = SDL_GetTicks();
                        read = true;
                        break;
                    }
                }
                if (!read) {
                    pick_up_message = "Nobody carries a castable scroll";
                    pick_up_shown = SDL_GetTicks();
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
                const std::size_t target = game::aimed_actor(
                    session, battle, camera.position, camera.forward(), game::kPartyReach);
                for (auto& caster : party) {
                    if (caster.hit_points <= 0 || cast) {
                        continue;
                    }
                    const data::SpellStatsEntry* best = nullptr;
                    data::SpellEffect best_effect;
                    int best_amount = 0;
                    for (const int id : caster.known_spells) {
                        const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                        if (spell == nullptr || caster.spell_points < spell->cost_normal) {
                            continue;
                        }
                        const data::SpellEffect effect = data::parse_spell_effect(*spell, 0);
                        const int amount = wounded ? effect.heal.high
                                                   : effect.damage.high +
                                                         effect.damage_per_skill.high *
                                                             caster.level;
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
                        party[worst].hit_points =
                            std::min(party[worst].max_hit_points,
                                     party[worst].hit_points + std::max(1, best_effect.heal.low));
                        pick_up_message = caster.name + " casts " +
                                          data::cp1252_to_utf8(best->name) + " on " +
                                          party[worst].name;
                    } else if (target != game::kNoActor) {
                        caster.spell_points -= best->cost_normal;
                        pick_up_message = battle.smite(
                            target, best_effect.damage, best_effect.damage_per_skill,
                            caster.level, best->element, caster.name, session, monster_stats,
                            item_stats, random_items, standard_bonuses, special_bonuses);
                    } else {
                        pick_up_message = "Nothing in reach to cast at";
                    }
                    pick_up_shown = SDL_GetTicks();
                    cast = true;
                }
                if (!cast) {
                    pick_up_message = "Nobody can cast what the moment needs";
                    pick_up_shown = SDL_GetTicks();
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

        // And monsters block the party, the way the party blocks them.
        if (!fly) {
            for (std::size_t i = 0; i < session.actors.size(); ++i) {
                if (!battle.alive(i)) {
                    continue;
                }
                camera.position = game::push_out_of(camera.position, session.actors[i].position,
                                                    game::kPartySpacing);
            }
        }

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

        // Outdoors, both the sun and the sky follow the clock.
        const render::Color sky = session.outdoor() ? game::sky_colour(clock) : indoor_sky;
        scene.begin(camera, sky);
        if (session.outdoor()) {
            draw_outdoor(scene, session, cache, game::sun_direction(clock),
                         game::light_level(clock));
        } else {
            draw_indoor(scene, session, cache, lamp);
        }
        mob.update(in.dt, session, camera.position,
                   [&](std::size_t actor) { return battle.alive(actor); });
        if (std::string blow = battle.update(in.dt, session, monster_stats, spell_stats, party,
                                             camera.position);
            !blow.empty()) {
            pick_up_message = std::move(blow);
            pick_up_shown = SDL_GetTicks();
        }
        party_recovery = std::max(0.0f, party_recovery - in.dt);
        clock.advance_seconds(in.dt);
        battle.award(party);
        // What the kills left: the gold goes to the purse, the items to
        // whichever pack has room, and one line names the lot.
        std::string found_text;
        if (const int found = battle.take_gold(); found > 0) {
            gold += found;
            found_text = std::to_string(found) + " gold";
        }
        for (const int id : battle.take_loot()) {
            const auto* row = item_stats.at(static_cast<std::size_t>(id));
            if (row == nullptr) {
                continue;
            }
            const render::Texture& icon = cache.icon(row->picture);
            const int w = std::max(1, game::cells_across(static_cast<int>(icon.width())));
            const int h = std::max(1, game::cells_across(static_cast<int>(icon.height())));
            for (auto& pack : packs) {
                if (pack.add(id, w, h)) {
                    found_text += (found_text.empty() ? "" : " and ") +
                                  data::cp1252_to_utf8(row->name);
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
            party[i].armor_class =
                game::attribute_bonus(party[i].attribute(game::Attribute::Speed)) +
                game::armour_of(party[i], item_stats) + party[i].temp_armor;
        }

        if (want_rest) {
            const bool disturbed =
                battle.anything_near(session, camera.position, game::kRestDisturbance);
            const game::RestResult result = game::rest(party, clock, disturbed);
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
        const game::AimedFace aimed = game::aimed_face(session, camera.position, camera.forward());
        if (want_strike && aimed.found()) {
            // The walker's view of the purse and the packs.
            script_state.gold = gold;
            script_state.items.clear();
            for (const auto& pack : packs) {
                for (const auto& carried : pack.items()) {
                    script_state.items.push_back(carried.item_id);
                }
            }
            const bool local = session.script.defines(aimed.event_id);
            const game::WalkOutcome outcome = game::walk_event(
                local ? session.script : global_script, aimed.event_id, script_state);
            gold = script_state.gold;

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
                    open_shop = static_cast<int>(i);
                    shop_stock =
                        game::stock_of(*shops_here[i], random_items, item_stats, standard_bonuses,
                                       special_bonuses, outcome.building * 2654435761U);
                    shop_said.clear();
                    break;
                }
            }
            // A chest gives up what the map's treasure level rolls.
            if (outcome.chest >= 0 && !opened_chests.contains(outcome.chest)) {
                opened_chests.insert(outcome.chest);
                std::string took;
                for (const int id : game::chest_contents(
                         static_cast<std::size_t>(session.treasure_level), random_items, item_stats,
                         standard_bonuses, special_bonuses,
                         static_cast<std::uint32_t>(outcome.chest + 1) * 40503U,
                         game::kChestItems)) {
                    const auto* row = item_stats.at(static_cast<std::size_t>(id));
                    if (row == nullptr) {
                        continue;
                    }
                    const render::Texture& icon = cache.icon(row->picture);
                    const int w = std::max(1, game::cells_across(static_cast<int>(icon.width())));
                    const int h = std::max(1, game::cells_across(static_cast<int>(icon.height())));
                    for (auto& pack : packs) {
                        if (pack.add(id, w, h)) {
                            took = data::cp1252_to_utf8(row->name);
                            break;
                        }
                    }
                }
                said_text = took.empty() ? "The chest is empty" : "You find " + took;
            }

            // A fountain's blessing: the walker keeps the seven attributes'
            // temporary bonuses in variables 25..31 — Might named by the
            // fountain's own words, the rest by position — and they lie on
            // the whole party until a rest. `inferred` for the party-wide
            // reach and the until.
            for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
                const int bonus = script_state.variables[25 + static_cast<int>(a)];
                if (bonus <= 0) {
                    continue;
                }
                for (auto& member : party) {
                    member.temp_attributes[a] = std::max(member.temp_attributes[a], bonus);
                }
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
                    std::string blow =
                        battle.strike(target, party[who], packs[who], session, monster_stats,
                                      item_stats, random_items, standard_bonuses, special_bonuses);
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
            draw_directory(scene, font, session, clock, trade_talk);
        }
        if (shown_member < 0 && shown_pack < 0 && open_shop < 0) {
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
                       stat_descriptions, class_descriptions, clock.minutes());
        }
        if (talking_to >= 0 && open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
            if (talking_to < static_cast<int>(here.size())) {
                draw_conversation(scene, font,
                                  game::talk_to(patched(here[static_cast<std::size_t>(
                                                    talking_to)]),
                                                dialogue, personalities, trade_talk, clock,
                                                interface_words, party[0].name,
                                                game::face_is_female(party[0].face)),
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
        // A face with a script on it names itself, which the inspect panel
        // shows the same way it names a monster.
        if (aimed.found() && shown_member < 0 && shown_pack < 0 && open_shop < 0) {
            if (const std::string named = game::face_name(session, aimed.event_id);
                !named.empty()) {
                game::draw_text(scene.framebuffer(), font, kWidth / 2 - font.text_width(named) / 2,
                                kHeight / 2 + 16, named, render::Color{225, 220, 190, 255},
                                render::Color{0, 0, 0, 255});
            }
        }
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
