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
#include "core/image/bitmap.hpp"
#include "core/image/font.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/render/scene.hpp"
#include "core/video/smacker.hpp"
#include "core/world/map_session.hpp"
#include "core/world/monster_spawn.hpp"
#include "core/world/texture_frame_table.hpp"
#include "game/ambient_mixer.hpp"
#include "game/body_magic.hpp"
#include "game/spell_damage.hpp"
#include "game/special_stats.hpp"
#include "game/spell_switch.hpp"
#include "game/buffs.hpp"
#include "game/spirit_mind_light.hpp"
#include "game/clock.hpp"
#include "game/fire_dark.hpp"
#include "game/combat.hpp"
#include "game/conversation.hpp"
#include "game/daylight.hpp"
#include "game/enchant.hpp"
#include "game/hire.hpp"
#include "game/inspect.hpp"
#include "game/inventory.hpp"
#include "game/interiors.hpp"
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
              << "  B          open the spell book: tabs by school, Enter readies a spell\n"
              << "             (--map opens the maps page, --title the title screen;\n"
              << "              --scale N sizes the window, default 2)\n"
              << "  F11        fullscreen on and off\n"
              << "  M          free the cursor: the frame's books, medallions and\n"
              << "             portraits answer clicks; M again returns to the view\n"
              << "  H          cast the readied spell, else the best heal or smite\n"
              << "  Space      strike whatever you are aiming at, in reach\n"
              << "  R          make camp: rest and heal, sleep to dawn, or wait\n"
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

// The sky behind the world: the header's own texture where the map names
// one, and the day's rolled sky where it does not — the original's picker
// at 0x46df60 re-rolls once per game day, 80% from the nine fair skies
// {1,3,6,7,8,9,12,14,15}, 20% from the seven others {2,5,10,13,16,18,19},
// its own tables at 0x4c1874/0x4c1898, with "sky01" the fallback. Wrapped
// as a cylinder around the camera's yaw and dimmed with the daylight; the
// projection is the engine's. `observed` for the roll and the tables.
void draw_sky(render::SceneRenderer& scene, assets::AssetCache& cache,
              const world::MapSession& session, float yaw, float pitch, float level,
              const std::string& rolled) {
    const std::string name =
        session.odm.header.sky_name.empty() ? rolled : session.odm.header.sky_name;
    const render::Texture& sky = cache.bitmap(name);
    if (sky.empty()) {
        return;
    }
    auto pixels = scene.framebuffer().color();
    const auto source = sky.pixels();
    const int tw = static_cast<int>(sky.width());
    const int th = static_cast<int>(sky.height());
    constexpr float kFov = 1.0472f;  // 60 degrees, the projection's own
    for (int y = 0; y < kHeight; ++y) {
        // The row's world pitch: negative looks above the horizon.
        const float row = (static_cast<float>(y) / kHeight - 0.5f) * kFov + pitch;
        const int tv = ((static_cast<int>((row + 1.6f) * static_cast<float>(th) * 0.6f) % th) +
                        th) % th;
        for (int x = 0; x < kWidth; ++x) {
            const float column = yaw + (static_cast<float>(x) / kWidth - 0.5f) * kFov;
            const int tu =
                ((static_cast<int>(column / 6.2832f * static_cast<float>(tw) * 4.0f) % tw) +
                 tw) % tw;
            const auto si = (static_cast<std::size_t>(tv) * static_cast<std::size_t>(tw) +
                             static_cast<std::size_t>(tu)) * 4;
            const auto di = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            pixels[di] = static_cast<std::uint8_t>(static_cast<float>(source[si]) * level);
            pixels[di + 1] = static_cast<std::uint8_t>(static_cast<float>(source[si + 1]) * level);
            pixels[di + 2] = static_cast<std::uint8_t>(static_cast<float>(source[si + 2]) * level);
        }
    }
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

// The schools a member has spells in, in the table's own order.
std::vector<starhaven::data::SpellSchool> schools_known(const game::Character& who) {
    std::vector<starhaven::data::SpellSchool> out;
    for (const int id : who.known_spells) {
        const auto school = static_cast<starhaven::data::SpellSchool>((id - 1) / 11);
        if (std::find(out.begin(), out.end(), school) == out.end()) {
            out.push_back(school);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The spell book, in the game's own art: the `Book` page fills the
// viewport, the `TAB` school tabs stand on its right edge, and each spell
// wears its `FIRE004`-style icon — the same school-and-number naming the
// projectiles fly by. This install ships no Water icons and only Light's
// emblem, so those spells stand as their names; the grid's spacing is the
// engine's own. `observed` for the names, `inferred` for the layout.
void draw_book(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
               const game::Character& who, const data::SpellStatsTable& spells, int school_index,
               int pick, int readied_id, int school_points) {
    blit(scene.framebuffer(), cache.icon("Book"), 8, 8);
    const auto schools = schools_known(who);
    if (schools.empty()) {
        game::draw_text(scene.framebuffer(), font, 40, 40, who.name + " knows no spells",
                        render::Color{60, 40, 20, 255}, render::Color{0, 0, 0, 0});
        return;
    }
    school_index = std::clamp(school_index, 0, static_cast<int>(schools.size()) - 1);
    const auto school = schools[static_cast<std::size_t>(school_index)];
    constexpr std::array<std::string_view, data::kSpellSchoolCount> kIconPrefix{
        "FIRE", "AIR", "WATER", "EARTH", "SPRT", "MIND", "BODY", "LIGHT", "DARK"};

    // The tabs: one per school with anything written under it.
    for (std::size_t i = 0; i < schools.size(); ++i) {
        const int number = static_cast<int>(schools[i]) + 1;
        const std::string tab =
            "TAB" + std::to_string(number) + (static_cast<int>(i) == school_index ? "B" : "A");
        blit(scene.framebuffer(), cache.icon(tab), 8 + 460 - 42, 24 + static_cast<int>(i) * 38);
    }

    // The page: the school's emblem, then its spells in a grid.
    const auto prefix = std::string(kIconPrefix[static_cast<std::size_t>(school)]);
    blit(scene.framebuffer(), cache.icon(prefix + "000"), 30, 16);
    game::draw_text(scene.framebuffer(), font, 166, 40,
                    std::string(data::school_name(school)) + ", skill " +
                        std::to_string(school_points),
                    render::Color{70, 45, 20, 255}, render::Color{0, 0, 0, 0});

    std::vector<const data::SpellStatsEntry*> page;
    for (const int id : who.known_spells) {
        const auto* spell = spells.at(id);
        if (spell != nullptr && spell->school == school) {
            page.push_back(spell);
        }
    }
    pick = page.empty() ? 0 : std::clamp(pick, 0, static_cast<int>(page.size()) - 1);
    auto pixels = scene.framebuffer().color();
    const auto box = [&](int x0, int y0, int w, int h, render::Color c) {
        for (int y = y0; y < y0 + h; ++y) {
            for (int x = x0; x < x0 + w; ++x) {
                if (y > y0 + 1 && y < y0 + h - 2 && x > x0 + 1 && x < x0 + w - 2) {
                    continue;
                }
                const auto i =
                    (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
                pixels[i] = c.r;
                pixels[i + 1] = c.g;
                pixels[i + 2] = c.b;
            }
        }
    };
    for (std::size_t i = 0; i < page.size(); ++i) {
        const int col = static_cast<int>(i) % 4;
        const int row = static_cast<int>(i) / 4;
        const int x = 32 + col * 106;
        const int y = 104 + row * 68;
        const auto& icon = cache.icon(prefix + (page[i]->number < 10 ? "00" : "0") +
                                      std::to_string(page[i]->number));
        if (!icon.empty()) {
            blit(scene.framebuffer(), icon, x, y);
        } else {
            game::draw_text(scene.framebuffer(), font, x + 4, y + 18,
                            data::cp1252_to_utf8(page[i]->short_name),
                            render::Color{70, 45, 20, 255}, render::Color{0, 0, 0, 0});
        }
        game::draw_text(scene.framebuffer(), font, x, y + 54,
                        data::cp1252_to_utf8(page[i]->short_name),
                        render::Color{80, 55, 25, 255}, render::Color{0, 0, 0, 0});
        if (static_cast<int>(i) == pick) {
            box(x - 3, y - 3, 100, 60, render::Color{170, 120, 30, 255});
        }
        if (page[i]->id == readied_id) {
            box(x - 6, y - 6, 106, 66, render::Color{60, 90, 160, 255});
        }
    }
    if (!page.empty()) {
        const auto* chosen = page[static_cast<std::size_t>(pick)];
        const int rank = game::rank_of(school_points);
        const int cost = rank >= 2   ? chosen->cost_master
                         : rank == 1 ? chosen->cost_expert
                                     : chosen->cost_normal;
        game::draw_text(scene.framebuffer(), font, 34, 326,
                        data::cp1252_to_utf8(chosen->name) + "  (" + std::to_string(cost) +
                            " sp)  Enter readies, B closes, 1-4 turn to a packmate",
                        render::Color{70, 45, 20, 255}, render::Color{0, 0, 0, 0});
    }
}

// The maps page the globe book opens: outdoors the 128x128 tilemap, each
// cell wearing its own ground art's average colour, indoors the floor
// plan traced from the BLV's upward faces — the cells and floors are the
// maps' own; the projection, the flat-colour reading and the party's
// arrow are the engine's. North is up, the way the wizard's eye draws.
// The eye's dots on the globe, by the ranks the spell's own words grant:
// monsters first, then treasure, then points of interest — here the map's
// event faces stand in for the points. The colours are the engine's.
template <typename ToScreen, typename Put>
void draw_map_eyes(const world::MapSession& session, const game::Battle& battle, int eye_rank,
                   const ToScreen& to_screen, const Put& put) {
    if (eye_rank < 1) {
        return;
    }
    const auto dot = [&](const render::Vec3& at, render::Color colour) {
        int x = 0, y = 0;
        to_screen(at, x, y);
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                put(x + ox, y + oy, colour);
            }
        }
    };
    for (std::size_t i = 0; i < session.actors.size(); ++i) {
        if (battle.alive(i)) {
            dot(session.actors[i].position, {200, 60, 50, 255});
        }
    }
    if (eye_rank >= 2) {
        for (const auto& object : session.objects) {
            dot(object.position, {220, 200, 80, 255});
        }
    }
}

void draw_map_page(render::SceneRenderer& scene, const world::MapSession& session,
                   const render::Vec3& party, const render::Vec3& forward,
                   std::array<render::Color, 256>& tile_colors, bool& colors_ready,
                   int eye_rank, const game::Battle& battle) {
    auto pixels = scene.framebuffer().color();
    const auto put = [&](int x, int y, render::Color c) {
        if (x < 8 || x >= 468 || y < 8 || y >= 352) {
            return;
        }
        const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
        pixels[i] = c.r;
        pixels[i + 1] = c.g;
        pixels[i + 2] = c.b;
    };
    for (int y = 8; y < 352; ++y) {
        for (int x = 8; x < 468; ++x) {
            put(x, y, {26, 22, 18, 255});
        }
    }
    const auto line = [&](int x0, int y0, int x1, int y1, render::Color c) {
        const int dx = std::abs(x1 - x0);
        const int dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            put(x0, y0, c);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    };

    float px = 0.0f;
    float py = 0.0f;
    float heading_x = forward.x;
    float heading_z = forward.z;
    if (session.outdoor()) {
        if (!colors_ready) {
            for (int t = 0; t < 256; ++t) {
                const auto& tex = session.tiles.texture_for(static_cast<std::uint8_t>(t));
                unsigned r = 0, g = 0, b = 0, n = 0;
                const auto source = tex.pixels();
                for (std::size_t i = 0; i + 3 < source.size(); i += 16) {
                    r += source[i];
                    g += source[i + 1];
                    b += source[i + 2];
                    ++n;
                }
                tile_colors[static_cast<std::size_t>(t)] =
                    n > 0 ? render::Color{static_cast<std::uint8_t>(r / n),
                                          static_cast<std::uint8_t>(g / n),
                                          static_cast<std::uint8_t>(b / n), 255}
                          : render::Color{40, 40, 40, 255};
            }
            colors_ready = true;
        }
        constexpr int dim = world::OdmTerrain::kGridDim;
        constexpr int kCell = 2;
        const int left = 8 + (460 - dim * kCell) / 2;
        const int top = 8 + (344 - dim * kCell) / 2;
        for (int gz = 0; gz < dim; ++gz) {
            for (int gx = 0; gx < dim; ++gx) {
                const auto tile =
                    session.terrain.tilemap[static_cast<std::size_t>(gz) * dim + gx];
                const render::Color c = tile_colors[tile];
                const int x = left + gx * kCell;
                const int y = top + (dim - 1 - gz) * kCell;
                for (int oy = 0; oy < kCell; ++oy) {
                    for (int ox = 0; ox < kCell; ++ox) {
                        put(x + ox, y + oy, c);
                    }
                }
            }
        }
        const render::TerrainScale scale{};
        const float half = (dim - 1) * scale.cell_size * 0.5f;
        const auto to_screen = [&](const render::Vec3& at, int& sx, int& sy) {
            sx = left + static_cast<int>((at.x + half) / scale.cell_size *
                                         static_cast<float>(kCell));
            sy = top + static_cast<int>((static_cast<float>(dim) - 1.0f -
                                         (at.z + half) / scale.cell_size) *
                                        static_cast<float>(kCell));
        };
        draw_map_eyes(session, battle, eye_rank, to_screen, put);
        px = static_cast<float>(left) + (party.x + half) / scale.cell_size * kCell;
        py = static_cast<float>(top) +
             (static_cast<float>(dim) - 1.0f - (party.z + half) / scale.cell_size) * kCell;
    } else {
        // The floor plan: every face that looks up, drawn edge by edge.
        float low_x = 1e9f, low_z = 1e9f, high_x = -1e9f, high_z = -1e9f;
        for (const auto& v : session.blv.vertices) {
            const render::Vec3 at = world::to_render_space(v.x, v.y, v.z);
            low_x = std::min(low_x, at.x);
            high_x = std::max(high_x, at.x);
            low_z = std::min(low_z, at.z);
            high_z = std::max(high_z, at.z);
        }
        const float span = std::max({high_x - low_x, high_z - low_z, 1.0f});
        const float fit = 320.0f / span;
        const int left = 8 + 230;
        const int top = 8 + 172;
        const auto place = [&](const render::Vec3& at, int& x, int& y) {
            x = left + static_cast<int>((at.x - (low_x + high_x) * 0.5f) * fit);
            y = top - static_cast<int>((at.z - (low_z + high_z) * 0.5f) * fit);
        };
        for (const auto& f : session.blv.faces) {
            // Up in the file is z: the collision build passes the normal
            // as (nx, nz, ny), so a floor is a face whose nz looks up.
            if (f.invisible() || f.vertex_count < 3 || f.nz() < 0.7f) {
                continue;
            }
            for (std::size_t k = 0; k < f.vertex_count; ++k) {
                const auto& a = session.blv.vertices[f.vertex_ids[k]];
                const auto& b = session.blv.vertices[f.vertex_ids[(k + 1) % f.vertex_count]];
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                place(world::to_render_space(a.x, a.y, a.z), x0, y0);
                place(world::to_render_space(b.x, b.y, b.z), x1, y1);
                line(x0, y0, x1, y1, {150, 140, 110, 255});
            }
        }
        draw_map_eyes(session, battle, eye_rank, place, put);
        int ix = 0, iy = 0;
        place(party, ix, iy);
        px = static_cast<float>(ix);
        py = static_cast<float>(iy);
    }

    // The party's arrow: a dot and the way it faces.
    const float norm = std::sqrt(heading_x * heading_x + heading_z * heading_z);
    if (norm > 0.001f) {
        heading_x /= norm;
        heading_z /= norm;
    }
    const int ax = static_cast<int>(px);
    const int ay = static_cast<int>(py);
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            put(ax + ox, ay + oy, {235, 60, 40, 255});
        }
    }
    line(ax, ay, ax + static_cast<int>(heading_x * 9.0f),
         ay - static_cast<int>(heading_z * 9.0f), {235, 60, 40, 255});
}

void draw_doll(render::SceneRenderer& scene, assets::AssetCache& cache,
               const data::ItemStatsTable& items, const game::Character& who);

// The quick reference: the game's own `QUIKREF` frame, whose five columns
// the art measures for us — a label column at x 17..80 and the four
// characters at 86, 180, 274 and 368 — with the party read across at a
// glance. `observed` for the columns, `inferred` for which rows to show.
void draw_quick_reference(render::SceneRenderer& scene, const image::Font& font,
                          assets::AssetCache& cache,
                          const std::array<game::Character, 4>& party,
                          const data::DescriptionTable& stats, int gold, int food) {
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
    blit(scene.framebuffer(), cache.icon("QUIKREF"), 8, 8);
    const render::Color white{230, 230, 230, 255};
    const render::Color dim{175, 175, 175, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    constexpr std::array<int, 4> kColumns{86 + 12, 180 + 12, 274 + 12, 368 + 12};

    game::draw_text(scene.framebuffer(), font, 26, 26, "Quick reference", white, shadow);
    for (std::size_t i = 0; i < party.size(); ++i) {
        game::draw_text(scene.framebuffer(), font, 8 + kColumns[i], 52, party[i].name, white,
                        shadow);
    }
    int y = 72;
    const auto row = [&](const std::string& label, auto value) {
        game::draw_text(scene.framebuffer(), font, 26, y, label, dim, shadow);
        for (std::size_t i = 0; i < party.size(); ++i) {
            game::draw_text(scene.framebuffer(), font, 8 + kColumns[i], y, value(party[i]),
                            white, shadow);
        }
        y += line;
    };
    row("Class", [](const game::Character& who) { return who.class_name; });
    row("Level", [](const game::Character& who) { return std::to_string(who.level); });
    for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
        row(std::string(game::stat_label(stats, a)).substr(0, 8),
            [a](const game::Character& who) {
                return std::to_string(who.attribute(static_cast<game::Attribute>(a)));
            });
    }
    row("Hits", [](const game::Character& who) {
        return std::to_string(who.hit_points) + "/" + std::to_string(who.max_hit_points);
    });
    row("Spells", [](const game::Character& who) {
        return std::to_string(who.spell_points) + "/" + std::to_string(who.max_spell_points);
    });
    row("Armour", [](const game::Character& who) { return std::to_string(who.armor_class); });
    row("Skills", [](const game::Character& who) { return std::to_string(who.skills.size()); });
    game::draw_text(scene.framebuffer(), font, 26, 326,
                    std::to_string(gold) + " gold, " + std::to_string(food) +
                        " food     (any key closes)",
                    dim, shadow);
}

// The options the engine actually has to offer, on the game's own panel.
void draw_options(render::SceneRenderer& scene, const image::Font& font,
                  assets::AssetCache& cache, int scale, bool fullscreen, bool turn_based,
                  bool always_run, bool loud_music) {
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
    blit(scene.framebuffer(), cache.icon("Options"), 8, 8);
    const render::Color white{235, 230, 205, 255};
    const render::Color dim{190, 185, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 4;
    int y = 40;
    game::draw_text(scene.framebuffer(), font, 34, y, "Options", white, shadow);
    y += line * 2;
    const auto say = [&](const std::string& text, bool on) {
        game::draw_text(scene.framebuffer(), font, 34, y, text + (on ? "  on" : "  off"), white,
                        shadow);
        y += line;
    };
    game::draw_text(scene.framebuffer(), font, 34, y,
                    "Window scale  " + std::to_string(scale) + "x   (--scale N)", white, shadow);
    y += line;
    say("Fullscreen    (F11)", fullscreen);
    say("Turn-based    (Enter)", turn_based);
    y += line;
    game::draw_text(scene.framebuffer(), font, 34, y, "From the install's own MM6.ini:", dim,
                    shadow);
    y += line;
    say("  AlwaysRun", always_run);
    say("  LoudMusic", loud_music);
    game::draw_text(scene.framebuffer(), font, 34, 320, "any key closes", dim, shadow);
}

// The character sheet, on the game's own four pages: fr_stats, fr_skill,
// fr_inven and fr_award each frame what their name says, and the arrows
// turn between them. The fields keep the design tables' own names.
void draw_sheet(render::SceneRenderer& scene, const image::Font& font, assets::AssetCache& cache,
                const game::Character& who, const data::DescriptionTable& stats,
                const data::DescriptionTable& classes, std::int64_t minute,
                const data::JournalTable& awards, const std::set<int>& earned, int page,
                const data::ItemStatsTable& items) {
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

    constexpr std::array<const char*, 4> kPages{"fr_stats", "fr_skill", "fr_inven", "fr_award"};
    constexpr std::array<const char*, 4> kTitles{"statistics", "skills", "inventory", "honors"};
    page = std::clamp(page, 0, 3);
    if (page != 2) {
        blit(scene.framebuffer(), cache.icon(kPages[static_cast<std::size_t>(page)]), 8, 100);
    }
    blit(scene.framebuffer(), cache.icon(game::portrait_entry(
                                  who.face, game::portrait_frame_of(who, false))),
         24, 28);
    game::draw_text(scene.framebuffer(), font, 100, 30, who.name, white, shadow);
    game::draw_text(scene.framebuffer(), font, 100, 30 + line,
                    who.class_name + ", level " + std::to_string(who.level), dim, shadow);
    game::draw_text(scene.framebuffer(), font, 300, 30,
                    std::string(kTitles[static_cast<std::size_t>(page)]) +
                        "  (left/right turn the page)",
                    dim, shadow);

    if (page == 0) {
        // The seven attributes, named and ordered by stats.txt itself.
        int y = 134;
        for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
            const std::string_view label = game::stat_label(stats, a);
            const int value = who.attribute(static_cast<game::Attribute>(a));
            const int bonus = game::attribute_bonus(value);
            std::string text = std::string(label) + "  " + std::to_string(value);
            if (who.temp_attributes[a] > 0) {
                text += " (of it +" + std::to_string(who.temp_attributes[a]) + " temporary)";
            }
            text += bonus == 0 ? ""
                               : (bonus > 0 ? "  +" + std::to_string(bonus)
                                            : "  " + std::to_string(bonus));
            game::draw_text(scene.framebuffer(), font, 24, y, text, white, shadow);
            y += line;
        }

        // The named conditions the potions set, with the sheet's own hours.
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

        // And the derived numbers, in the same order the table lists them.
        y = 134;
        const std::array<std::pair<std::size_t, std::string>, 5> derived{{
            {7, std::to_string(who.hit_points) + " / " + std::to_string(who.max_hit_points)},
            {8, std::to_string(who.armor_class)},
            {9, std::to_string(who.spell_points) + " / " + std::to_string(who.max_spell_points)},
            {12, std::to_string(who.age)},
            {14, std::to_string(who.experience)},
        }};
        for (const auto& [row, value] : derived) {
            game::draw_text(scene.framebuffer(), font, 260, y,
                            std::string(game::stat_label(stats, row)) + "  " + value, white,
                            shadow);
            y += line;
        }

        // What the class is, in the designers' own words, below.
        if (const auto* described = classes.find(who.class_name);
            described != nullptr && !described->text.empty()) {
            const std::string text = data::cp1252_to_utf8(described->text.front());
            int x = 24;
            int wrap_y = 300;
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
    } else if (page == 1) {
        // Every skill held, with what raising it would cost. The staircase
        // is the engine's own; the effects are the table's lines.
        int y = 134;
        game::draw_text(scene.framebuffer(), font, 24, y,
                        "Skills  (" + std::to_string(who.skill_points) + " to spend)", white,
                        shadow);
        y += line;
        int numbered = 5;
        for (const auto& [skill, packed] : who.skills) {
            // The stored byte is points and rank together; the sheet used to
            // print it raw, so a master's five points read as 133.
            const int points = game::skill_points(packed);
            const int rank = game::skill_rank(packed);
            std::string label = (numbered <= 9 ? std::to_string(numbered) + "  " : "   ") +
                                skill + "  " + std::to_string(points) + " " +
                                std::string(game::kRankNames[static_cast<std::size_t>(rank)]) +
                                "  (raise for " + std::to_string(game::raise_cost(points)) + ")";
            if (rank < 2) {
                label += ", " + std::string(game::kRankNames[static_cast<std::size_t>(rank + 1)]) +
                         " for " + std::to_string(game::teach_price(rank + 1)) + " gold";
            }
            game::draw_text(scene.framebuffer(), font, 24, y, label,
                            who.skill_points >= game::raise_cost(points) ? white : dim, shadow);
            y += line;
            ++numbered;
            if (y > kHeight - line * 2) {
                break;
            }
        }
    } else if (page == 2) {
        draw_doll(scene, cache, items, who);
        game::draw_text(scene.framebuffer(), font, 24, kHeight - line - 8,
                        "the pack key opens the grid itself", dim, shadow);
    } else {
        // The honors the quests set, worded by Awards.txt itself — all of
        // them, on their own page.
        int y = 134;
        game::draw_text(scene.framebuffer(), font, 24, y, "Honors", white, shadow);
        y += line;
        for (const auto& row : awards.entries()) {
            if (!earned.contains(row.bit) || !row.has_text()) {
                continue;
            }
            if (y > kHeight - line * 2) {
                game::draw_text(scene.framebuffer(), font, 32, y, "...", dim, shadow);
                break;
            }
            game::draw_text(scene.framebuffer(), font, 32, y, data::cp1252_to_utf8(row.text),
                            dim, shadow);
            y += line;
        }
        if (earned.empty()) {
            game::draw_text(scene.framebuffer(), font, 32, y, "none yet", dim, shadow);
        }
    }
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

// Shaping the party before the world starts, in the game's own hall:
// `Makeme.pcx` is the whole screen — four marble columns, each with an
// oval seat, a name plate and a green slab — under the `MAKETOP` band.
// The seats sit at x 17, 176, 334 and 493, measured from the art. The
// twelve portraits, the six base classes and the names are the game's
// own; the rolled numbers are this engine's and the sheet says so.
void draw_creation(render::SceneRenderer& scene, const image::Font& font,
                   assets::AssetCache& cache, const std::array<game::Character, 4>& party,
                   int slot, const data::DescriptionTable& stats,
                   const data::DescriptionTable& classes) {
    if (font.glyph_count() == 0) {
        return;
    }
    // MAKETOP is cut out where a sky shows through; MAKESKY is that sky.
    blit(scene.framebuffer(), cache.icon("MAKESKY"), 0, 0);
    blit(scene.framebuffer(), cache.icon("MAKETOP"), 0, 0);
    blit(scene.framebuffer(), cache.icon("Makeme.pcx"), 0, 23);

    const render::Color white{230, 230, 230, 255};
    const render::Color dim{190, 190, 185, 255};
    const render::Color mark{235, 225, 170, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    constexpr std::array<int, 4> kSeats{17, 176, 334, 493};

    for (int i = 0; i < 4; ++i) {
        const auto& who = party[static_cast<std::size_t>(i)];
        const int x = kSeats[static_cast<std::size_t>(i)];
        blit(scene.framebuffer(), cache.icon(game::portrait_entry(who.face)), x, 35);
        // The name on its plate, the class atop the green slab.
        game::draw_text(scene.framebuffer(), font, x - 6, 124,
                        std::to_string(i + 1) + " " + who.name, i == slot ? mark : dim, shadow);
        int y = 152;
        game::draw_text(scene.framebuffer(), font, x - 6, y, who.class_name,
                        i == slot ? mark : white, shadow);
        y += line + 4;
        for (std::size_t a = 0; a < game::kAttributeCount; ++a) {
            game::draw_text(scene.framebuffer(), font, x - 6, y,
                            std::string(game::stat_label(stats, a)).substr(0, 4) + "  " +
                                std::to_string(who.attribute(static_cast<game::Attribute>(a))),
                            white, shadow);
            y += line;
        }
        y += 4;
        game::draw_text(scene.framebuffer(), font, x - 6, y,
                        "hp " + std::to_string(who.max_hit_points) + "  sp " +
                            std::to_string(who.max_spell_points),
                        dim, shadow);
    }

    // The wide panel along the bottom: the chosen one's class, described.
    const auto& who = party[static_cast<std::size_t>(slot)];
    if (const auto* described = classes.find(who.class_name);
        described != nullptr && !described->text.empty()) {
        const std::string text = data::cp1252_to_utf8(described->text.front());
        std::string word;
        int x = 44;
        int y = 396;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            const char ch = i < text.size() ? text[i] : ' ';
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > 470) {
                x = 44;
                y += line;
            }
            if (y < kHeight - line - 4) {
                game::draw_text(scene.framebuffer(), font, x, y, word, white, shadow);
            }
            x += width;
            word.clear();
        }
    }
    game::draw_text(scene.framebuffer(), font, 8, 6,
                    "1-4 choose, C class, F face, N name, R reroll (the rolls are this "
                    "engine's own), Enter begins",
                    dim, shadow);
}

// The journal: what the held quest bits say, in Quests.txt's own words,
// and the honors under them. The walker has kept this state all along;
// this is the first page it is written on.
void draw_journal(render::SceneRenderer& scene, const image::Font& font,
                  const data::JournalTable& quests, const data::JournalTable& awards,
                  const std::set<int>& bits, const std::set<int>& earned,
                  const data::JournalTable& notes, const std::set<int>& collected, int page) {
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
    game::draw_text(scene.framebuffer(), font, 24, y,
                    page == 0 ? "Journal  (left/right: the chronicle)"
                              : "Chronicle  (left/right: the journal)",
                    white, shadow);
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

    if (page == 1) {
        // The obelisk grid first: the fifteen fragments are columns, and
        // side by side they spell the sentence. A missing stone leaves
        // its column dark.
        bool any_stone = false;
        std::array<std::string, 15> columns{};
        for (int c = 0; c < 15; ++c) {
            const int note = 79 + c;
            if (!collected.contains(note)) {
                continue;
            }
            for (const auto& row : notes.entries()) {
                if (row.bit != note || !row.has_text()) {
                    continue;
                }
                std::string text = data::cp1252_to_utf8(row.text);
                if (const auto colon = text.find(':'); colon != std::string::npos) {
                    text = text.substr(colon + 1);
                }
                std::string cleaned;
                for (const char ch : text) {
                    if (ch == '_') {
                        cleaned += ' ';
                    } else if (ch != ' ' || !cleaned.empty()) {
                        cleaned += ch == '"' ? ' ' : ch;
                    }
                }
                while (!cleaned.empty() && cleaned.front() == ' ') {
                    cleaned.erase(cleaned.begin());
                }
                columns[static_cast<std::size_t>(c)] = cleaned;
                any_stone = true;
            }
        }
        if (any_stone) {
            std::size_t depth = 0;
            for (const auto& column : columns) {
                depth = std::max(depth, column.size());
            }
            game::draw_text(scene.framebuffer(), font, 24, y, "The obelisks:", white, shadow);
            y += line;
            for (std::size_t r = 0; r < depth && y < kHeight - line * 3; ++r) {
                std::string across;
                for (const auto& column : columns) {
                    across += column.empty() ? '?'
                              : r < column.size() ? column[r]
                                                  : ' ';
                }
                game::draw_text(scene.framebuffer(), font, 24, y, across, white, shadow);
                y += line - 2;
            }
            y += line;
        }
        // The chronicle: what the events wrote as the story advanced,
        // worded by Autonotes.txt itself, newest stages last.
        std::size_t written = 0;
        for (const auto& row : notes.entries()) {
            if (!collected.contains(row.bit) || !row.has_text() ||
                (row.bit >= 79 && row.bit <= 93)) {
                continue;
            }
            ++written;
            wrap("\x95 " + data::cp1252_to_utf8(row.text), white);
            y += 2;
            if (y >= kHeight - line * 3) {
                game::draw_text(scene.framebuffer(), font, 24, y, "...", dim, shadow);
                break;
            }
        }
        if (written == 0) {
            wrap("Nothing written yet.", dim);
        }
        return;
    }
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
// The paperdoll, shared by the pack screen and the sheet's inventory page.
void draw_doll(render::SceneRenderer& scene, assets::AssetCache& cache,
               const data::ItemStatsTable& items, const game::Character& who) {
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

}

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

    draw_doll(scene, cache, items, who);

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
// The service screens' shared dressing: the darkened room, `BACKEVT`'s
// marble panel on the left with the establishment's name, trade and
// proprietor written down it, and the footer strip carrying the keys.
// The words keep to the panel's right.
void dress_service(render::SceneRenderer& scene, const image::Font& font,
                   assets::AssetCache& cache, const data::BuildingStatsEntry& shop) {
    auto pixels = scene.framebuffer().color();
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto i = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
            pixels[i] = static_cast<std::uint8_t>(pixels[i] / 6);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 6);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 6);
        }
    }
    // The establishment's own room, when the Picture column's video is in
    // the install: its first frame fills the viewport, dimmed to half so
    // the words read over it. Without it, the marble panel as before.
    const std::string_view video = game::interior_video(shop.picture);
    const render::Texture& room =
        video.empty() ? cache.interior(std::string()) : cache.interior(std::string(video));
    const int line = font.height() + 2;
    if (!room.empty()) {
        blit(scene.framebuffer(), room, 8, 8);
        for (int y = 8; y < 352; ++y) {
            for (int x = 8; x < 468; ++x) {
                const auto i =
                    (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
                pixels[i] = static_cast<std::uint8_t>(pixels[i] / 2);
                pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 2);
                pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 2);
            }
        }
        int y = 44;
        for (const std::string text : {data::cp1252_to_utf8(shop.name), std::string(shop.type),
                                       data::cp1252_to_utf8(shop.proprietor)}) {
            game::draw_text(scene.framebuffer(), font, 26, y, text,
                            render::Color{225, 220, 195, 255}, render::Color{0, 0, 0, 255});
            y += line + 2;
        }
    } else {
        blit(scene.framebuffer(), cache.icon("BACKEVT"), 16, 40);
        int y = 56;
        for (const std::string text : {data::cp1252_to_utf8(shop.name), std::string(shop.type),
                                       data::cp1252_to_utf8(shop.proprietor)}) {
            game::draw_text(scene.framebuffer(), font, 26, y, text,
                            render::Color{225, 220, 195, 255}, render::Color{0, 0, 0, 255});
            y += line + 2;
        }
    }
    blit(scene.framebuffer(), cache.icon("FOOTER"), 0, kHeight - 24);
}

void draw_temple(render::SceneRenderer& scene, const image::Font& font,
                assets::AssetCache& cache,
                 const data::BuildingStatsEntry& shop,
                 const std::array<game::Character, 4>& party, int gold,
                 const std::string& said) {
    if (font.glyph_count() == 0) {
        return;
    }
    dress_service(scene, font, cache, shop);
    const render::Color white{230, 230, 230, 255};
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 44;
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
    game::draw_text(scene.framebuffer(), font, 190, y, terms, dim, shadow);
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
        game::draw_text(scene.framebuffer(), font, 190, y, text, needs ? white : dim, shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 190, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                    "1-4 heal a character, 5 donate, T talk, B closes", dim, shadow);
}

// A bank's counter: the balance, and the sheet's own two verbs.
void draw_bank(render::SceneRenderer& scene, const image::Font& font,
                assets::AssetCache& cache,
               const data::BuildingStatsEntry& shop, int gold, int balance,
               const std::string& said) {
    if (font.glyph_count() == 0) {
        return;
    }
    dress_service(scene, font, cache, shop);
    const render::Color white{230, 230, 230, 255};
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 44;
    game::draw_text(scene.framebuffer(), font, 190, y,
                    "you carry " + std::to_string(gold) + " gold; the vault holds " +
                        std::to_string(balance),
                    dim, shadow);
    y += line * 2;
    game::draw_text(scene.framebuffer(), font, 190, y, "1  deposit 100", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 190, y, "2  deposit all", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 190, y, "3  withdraw 100", white, shadow);
    y += line;
    game::draw_text(scene.framebuffer(), font, 190, y, "4  withdraw all", white, shadow);
    y += line;
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 190, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                    "T talk to whoever is here, B closes", dim, shadow);
}

// A training hall's counter: who can train, to what, and for how much.
void draw_training(render::SceneRenderer& scene, const image::Font& font,
                assets::AssetCache& cache,
                   const data::BuildingStatsEntry& shop,
                   const std::array<game::Character, 4>& party, int gold,
                   const std::string& said) {
    if (font.glyph_count() == 0) {
        return;
    }
    dress_service(scene, font, cache, shop);
    const render::Color white{230, 230, 230, 255};
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 44;
    std::string ceiling = "you have " + std::to_string(gold) + " gold";
    if (const int top = game::max_level_of(shop); top > 0) {
        ceiling += "; this hall trains to level " + std::to_string(top);
    }
    game::draw_text(scene.framebuffer(), font, 190, y, ceiling, dim, shadow);
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
        game::draw_text(scene.framebuffer(), font, 190, y, text, ready ? white : dim, shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 190, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                    "1-4 train a character, T talk to whoever is here, B closes", dim, shadow);
}

// A travel counter: where the rides go, when they leave, and the fare.
void draw_travel(render::SceneRenderer& scene, const image::Font& font,
                assets::AssetCache& cache,
                 const data::BuildingStatsEntry& shop,
                 const std::vector<game::TravelRoute>& routes, int fare,
                 const game::GameClock& clock, int gold, const std::string& said) {
    if (font.glyph_count() == 0) {
        return;
    }
    dress_service(scene, font, cache, shop);
    const render::Color white{230, 230, 230, 255};
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 44;
    game::draw_text(scene.framebuffer(), font, 190, y,
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
        game::draw_text(scene.framebuffer(), font, 190, y, text,
                        route.leaves_on(clock.day()) && fare <= gold ? white : dim, shadow);
        y += line;
    }
    if (routes.empty()) {
        game::draw_text(scene.framebuffer(), font, 190, y, "No rides leave from here.", dim,
                        shadow);
        y += line;
    }
    if (!said.empty()) {
        y += line;
        game::draw_text(scene.framebuffer(), font, 190, y, said, render::Color{235, 225, 170, 255},
                        shadow);
    }
    game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                    "1-3 ride, T talk to whoever is here, B closes", dim, shadow);
}

// A shop's counter: what it has, what it wants for it, and what the
// shopkeeper says about the state of your purse.
void draw_shop(render::SceneRenderer& scene, const image::Font& font,
                assets::AssetCache& cache,
               const data::BuildingStatsEntry& shop, const std::vector<game::StockItem>& stock,
               const data::ItemStatsTable& items, const data::MerchantTextTable& words, int gold,
               const std::string& said, int pick) {
    if (font.glyph_count() == 0) {
        return;
    }
    dress_service(scene, font, cache, shop);

    const render::Color white{230, 230, 230, 255};
    const render::Color dim{165, 165, 165, 255};
    const render::Color shadow{0, 0, 0, 255};
    const int line = font.height() + 2;
    int y = 44;
    game::draw_text(scene.framebuffer(), font, 190, y, "you have " + std::to_string(gold) + " gold",
                    dim, shadow);
    y += line * 2;

    // The goods themselves, laid out on the room in a grid: each item's
    // own picture, its price under it, the arrows walking the shelf and
    // Enter buying what the gold border holds. The grid's spacing is the
    // engine's own.
    const std::size_t shown = std::min<std::size_t>(stock.size(), 9);
    auto pixels = scene.framebuffer().color();
    const auto box = [&](int x0, int y0, int w, int h, render::Color c) {
        for (int by = y0; by < y0 + h; ++by) {
            for (int bx = x0; bx < x0 + w; ++bx) {
                if (by > y0 + 1 && by < y0 + h - 2 && bx > x0 + 1 && bx < x0 + w - 2) {
                    continue;
                }
                if (bx < 8 || bx >= 468 || by < 8 || by >= 352) {
                    continue;
                }
                const auto i =
                    (static_cast<std::size_t>(by) * kWidth + static_cast<std::size_t>(bx)) * 4;
                pixels[i] = c.r;
                pixels[i + 1] = c.g;
                pixels[i + 2] = c.b;
            }
        }
    };
    // A rack, not a table: the art hangs at its full length in narrow
    // slots, the way the game's own shelves overlap their goods.
    const int pitch = shown > 0 ? std::min<int>(120, 420 / static_cast<int>(shown)) : 0;
    for (std::size_t i = 0; i < shown; ++i) {
        const auto* row = items.at(static_cast<std::size_t>(stock[i].item_id));
        if (row == nullptr) {
            continue;
        }
        const int cx = 28 + static_cast<int>(i) * pitch;
        const auto& picture = cache.icon(row->picture);
        if (!picture.empty()) {
            blit(scene.framebuffer(), picture, cx, 64);
        }
        game::draw_text(scene.framebuffer(), font, cx, 300 + (static_cast<int>(i) % 2) * 14,
                        std::to_string(i + 1) + " " + std::to_string(stock[i].price) + "g",
                        stock[i].price <= gold ? white : dim, shadow);
        if (static_cast<int>(i) == pick) {
            box(cx - 4, 58, pitch, 256, render::Color{200, 160, 40, 255});
        }
    }
    if (!stock.empty() && pick >= 0 && static_cast<std::size_t>(pick) < shown) {
        if (const auto* row = items.at(static_cast<std::size_t>(
                stock[static_cast<std::size_t>(pick)].item_id));
            row != nullptr) {
            game::draw_text(scene.framebuffer(), font, 190, y,
                            data::cp1252_to_utf8(row->name) + "  " +
                                std::to_string(stock[static_cast<std::size_t>(pick)].price) +
                                " gold",
                            white, shadow);
        }
    }
    if (stock.empty()) {
        game::draw_text(scene.framebuffer(), font, 190, y, "The shelves are bare.", dim, shadow);
    }

    // The shopkeeper's own words, from Merchant.txt.
    if (!said.empty()) {
        game::draw_text(scene.framebuffer(), font, 36, 330, said,
                        render::Color{235, 225, 170, 255}, shadow);
    }
    game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                    "arrows pick, Enter buys, 1-9 too, S sell, F repair, T talk, B closes", dim,
                    shadow);
}

// Somebody in an establishment, and what they have to say.
// The talk screen, dressed in the game's own pieces: `BACKEVT`'s marble
// side panel on the left, and the speaker's `NPC###` plate — the portraits
// are named by their `NPCdata.txt` row, 396 of the 398 shipped plates
// joining exactly (553 and 554 stand apart, unclaimed). A passer-by with
// no row keeps the panel and goes faceless, honestly.
void draw_conversation(render::SceneRenderer& scene, const image::Font& font,
                       assets::AssetCache& cache, int npc_id, const game::Conversation& talk,
                       const std::string& answer,
                       const data::BuildingStatsEntry* shop = nullptr) {
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

    // Inside an establishment the room itself is the backdrop, dimmed to
    // half so the words read; in the street, the marble panel.
    const std::string_view video =
        shop != nullptr ? game::interior_video(shop->picture) : std::string_view{};
    const render::Texture& room =
        video.empty() ? cache.interior(std::string()) : cache.interior(std::string(video));
    if (!room.empty()) {
        blit(scene.framebuffer(), room, 8, 8);
        for (int y = 8; y < 352; ++y) {
            for (int x = 8; x < 468; ++x) {
                const auto i =
                    (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
                pixels[i] = static_cast<std::uint8_t>(pixels[i] / 2);
                pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] / 2);
                pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] / 2);
            }
        }
    } else {
        blit(scene.framebuffer(), cache.icon("BACKEVT"), 16, 40);
    }
    // The establishment's own side panel over the room, where the
    // executable's interior table names one — its byte 0, traced.
    if (shop != nullptr) {
        if (const int panel = game::interior_panel(shop->picture); panel > 0) {
            char plate[12];
            std::snprintf(plate, sizeof(plate), "EVPAN%03d", panel);
            blit(scene.framebuffer(), cache.icon(plate), 8, 8);
        }
    }
    if (npc_id > 0 && npc_id < 1000) {
        char plate[8];
        std::snprintf(plate, sizeof(plate), "NPC%03d", npc_id);
        blit(scene.framebuffer(), cache.icon(plate), 60, 64);
    }
    game::draw_text(scene.framebuffer(), font, 28, 150, talk.who, white, shadow);

    constexpr int kTalkLeft = 190;
    int y = 44;
    if (!talk.greeting.empty()) {
        game::draw_text(scene.framebuffer(), font, kTalkLeft, y, talk.greeting, said, shadow);
        y += line * 2;
    }
    if (!talk.today.empty()) {
        game::draw_text(scene.framebuffer(), font, kTalkLeft, y, "today: " + talk.today, dim,
                        shadow);
        y += line * 2;
    }
    for (std::size_t i = 0; i < talk.topics.size(); ++i) {
        game::draw_text(scene.framebuffer(), font, kTalkLeft, y,
                        std::to_string(i + 1) + "  " + talk.topics[i], white, shadow);
        y += line;
    }
    if (!answer.empty()) {
        y += line;
        // The answers run long, so they wrap.
        std::string word;
        int x = 190;
        for (std::size_t i = 0; i <= answer.size(); ++i) {
            const char ch = i < answer.size() ? answer[i] : ' ';
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const int width = font.text_width(word + " ");
            if (x + width > kWidth - 24) {
                x = 190;
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
    bool start_book = false;     // --book: open the spell book at once
    bool start_map = false;      // --map: open the maps page at once
    bool start_title = false;    // --title: hold the title screen for a capture
    bool start_rest = false;     // --rest: open the campfire for a capture
    bool start_quickref = false;  // --quickref: open the party at a glance
    int window_scale = 2;        // --scale N: the window's integer multiple
    bool fullscreen = false;     // F11 flips it
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
        } else if (a == "--book") {
            start_book = true;
        } else if (a == "--map") {
            start_map = true;
        } else if (a == "--title") {
            start_title = true;
        } else if (a == "--rest") {
            start_rest = true;
        } else if (a == "--quickref") {
            start_quickref = true;
        } else if (a == "--scale" && i + 1 < argc) {
            window_scale = std::clamp(std::atoi(argv[++i]), 1, 6);
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

    // The install's own MM6.ini, honored where its keys map: AlwaysRun
    // swaps the shift key's meaning, LoudMusic picks the music gain.
    // FlipOnExit has nothing here to flip and is left alone, noted.
    bool ini_always_run = false;
    bool ini_loud_music = true;
    {
        std::ifstream ini(data_dir.parent_path() / "MM6.ini");
        std::string ini_line;
        while (std::getline(ini, ini_line)) {
            if (ini_line.rfind("AlwaysRun=", 0) == 0) {
                ini_always_run = ini_line.back() == '1';
            } else if (ini_line.rfind("LoudMusic=", 0) == 0) {
                ini_loud_music = ini_line.back() == '1';
            }
        }
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
    // The honest 640x480, presented at an integer multiple: the window
    // grows, the pixels stay square, and every screen keeps its measures.
    SDL_Window* window =
        SDL_CreateWindow(title.c_str(), kWidth * window_scale, kHeight * window_scale, 0);
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderLogicalPresentation(sdl_renderer, kWidth, kHeight,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_Texture* screen = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ABGR8888,
                                            SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);

    const bool mouse_look = screenshot.empty();
    if (mouse_look) {
        SDL_SetWindowRelativeMouseMode(window, true);
    }
    // M frees the cursor to work the frame's own buttons; M again returns
    // it to the view.
    bool cursor_free = false;
    bool show_calendar = false;
    bool show_map = start_map;
    std::array<render::Color, 256> map_tile_colors{};
    bool map_colors_ready = false;

    // A one-frame capture ends before a note sounds, so do not open audio
    // devices for it at all.
    game::MusicPlayer music;
    music.set_loud(ini_loud_music);
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
    data::JournalTable autonote_texts;
    (void)data::load_autonotes(data_dir, autonote_texts);
    data::DescriptionTable skill_table;
    (void)data::load_descriptions(data_dir, "SkillDes.txt", skill_table);
    std::array<game::Character, 4> party = game::make_party(given_names, 1);
    // The party's sixteen buff slots, on the executable's own array.
    game::PartyBuffs party_buffs;
    // How long since the party last slept, in world hours, and the hour it
    // was last counted at. The original keeps this as a byte and wears the
    // party down with it; see src/game/clock.hpp.
    int hours_awake = 0;
    std::int64_t fatigue_hour = 0;
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
    // The game opens like the game: the title painting and its four
    // plates. New Game walks into the creation hall, Load into the saved
    // slot, Exit out — and the world holds its breath underneath.
    // The opening movies, and the credits when asked: any key skips the
    // one that plays. The decoder itself lives with the room player below.
    std::vector<std::string> movie_queue;
    bool at_title = start_title || (creating && !force_create);
    if (at_title && !start_title && screenshot.empty()) {
        movie_queue = {"3dologo", "MM6Intro"};
    }
    bool title_credits = false;
    if (at_title) {
        creating = false;
        if (mouse_look) {
            SDL_SetWindowRelativeMouseMode(window, false);
        }
    }
    int create_slot = 0;
    Mm6Random create_random{0x51C7E3A9u};
    std::array<game::Pack, 4> packs;
    int shown_member = open_sheet >= 1 && open_sheet <= 4 ? open_sheet - 1 : -1;
    int sheet_page = 0;  // which of the sheet's four framed pages shows
    int journal_page = 0;  // 0 the quests, 1 the chronicle
    bool show_quickref = start_quickref;
    bool show_options = false;
    // The arena's tournament, rebuilt engine-side: the original kept its
    // waves and purses in the executable, so every number here — the four
    // ranks' level bands, counts and prizes — is this engine's own and
    // says so. The counter awards 84..87 are the table's.
    int arena_rank = -1;   // -1 no challenge; 0..3 Page..Lord
    Mm6Random arena_random{20260730};
    int water_phase = -1;  // last baked step of the sea's palette ring
    std::int64_t sky_day = -1;      // the day the sky was last rolled
    std::string sky_today = "sky01";
    // The bounty board, rebuilt engine-side: the original kept its posting
    // and purse in the executable, so the month's length (28 days), the
    // pick and the level-times-hundred purse are this engine's numbers,
    // marked. Award 81 and its count are the table's.
    std::set<int> kills_this_month;
    int bounty_month_paid = -1;
    // The spell book: whose is open, which school tab shows, which spell is
    // under the finger, and what each member keeps readied for the cast key.
    int book_member = -1;
    if (start_book) {
        for (std::size_t i = 0; i < party.size(); ++i) {
            if (!party[i].known_spells.empty()) {
                book_member = static_cast<int>(i);
                break;
            }
        }
    }
    int book_school = 0;
    int book_pick = 0;
    std::array<int, 4> readied{};
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
    int shop_pick = 0;   // which shelf cell the gold border holds
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

    // The wall textures that move: DTFT.BIN's four loops, stepped on the
    // frame tables' shared clock; each tick the shown frame is copied over
    // the group's first name so the ordinary lookup draws the motion.
    std::vector<world::TextureAnimation> texture_loops;
    {
        lod::LodArchive icons_archive;
        std::span<const std::byte> raw;
        if (lod::LodArchive::open(data_dir / "icons.lod", icons_archive) == lod::LodError::None &&
            icons_archive.payload("DTFT.BIN", raw) == lod::LodArchive::PayloadError::None) {
            texture_loops = world::parse_texture_frames(raw);
        }
    }
    std::vector<std::string> texture_loop_shown(texture_loops.size());

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
        std::set<int> seated;
        for (const auto& b : session.buildings) {
            for (const auto& person : b.people) {
                const auto moved = script_state.npc_places.find(person.npc_id);
                const bool placed_here = moved != script_state.npc_places.end()
                                             ? moved->second == shop.id
                                             : b.building_id == shop.id;
                if (placed_here) {
                    seated.insert(person.npc_id);
                    out.push_back(person);
                }
            }
        }
        // Whoever the chain sent here from anywhere else — Archibald to
        // his library — arrives off the full roster.
        for (const auto& person : session.everyone) {
            const auto moved = script_state.npc_places.find(person.npc_id);
            if (moved != script_state.npc_places.end() && moved->second == shop.id &&
                !seated.contains(person.npc_id)) {
                out.push_back(person);
            }
        }
        return out;
    };
    // A door in the world: the entrance establishments — Dungeon Ent,
    // Castle Ent and their kin — join their maps by name: 34 of the 39
    // entrance rows match a MapStats display name exactly; the five
    // regional lords' castles match none and are video-only thrones in
    // the original, left as they are. `observed`
    const auto entrance_map_of = [&](const data::BuildingStatsEntry& shop) -> std::string {
        if (shop.type.find("Ent") == std::string::npos) {
            return {};
        }
        const std::string want = data::cp1252_to_utf8(shop.name);
        for (const auto& m : map_stats.entries()) {
            if (data::cp1252_to_utf8(m.name) == want) {
                return m.file_name;
            }
        }
        return {};
    };

    // The King's Library wears its three faces by the story: the statue
    // until award 35, the freed loop until Archibald hands over the
    // Ritual (bit 177), and the empty room after. The three pictures are
    // the rows' own; the gating between them is the engine's. `inferred`
    const auto face_of = [&](const data::BuildingStatsEntry& shop) {
        if (shop.id == 168 || shop.id == 553 || shop.id == 554) {
            return !script_state.awards.contains(35)  ? 116
                   : !script_state.bits.contains(177) ? 117
                                                      : 118;
        }
        return shop.picture;
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
    int hourglass_turn = 0;  // rounds resolved, for the HGLAS frames
    constexpr float kRoundSeconds = 1.0f;
    int striker = 0;  // whose turn it is to swing

    // Time, and when this map is next due to refill. The interval is the map's
    // own: see docs/formats/text-tables.md.
    game::GameClock clock;

    // The month's posted bounty: a hostile row picked by the month's own
    // number, deterministic so every hall posts the same head.
    const auto bounty_month = [&] { return static_cast<int>(clock.day() / 28); };
    const auto bounty_of = [&]() -> const data::MonsterStatsEntry* {
        std::vector<const data::MonsterStatsEntry*> pool;
        for (const auto& row : monster_stats.entries()) {
            if (row.hostility > 0 && row.level >= 3) {
                pool.push_back(&row);
            }
        }
        if (pool.empty()) {
            return nullptr;
        }
        const auto pick = static_cast<std::size_t>(bounty_month() * 2654435761U % pool.size());
        return pool[pick];
    };

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
        data::SpellReach reach = data::SpellReach::Single;  // what the prose reaches
    };
    std::vector<SpellShot> spell_shots;
    struct SpellBurst {
        std::string animation;
        render::Vec3 position;
        std::uint64_t until = 0;  // SDL ticks
    };
    std::vector<SpellBurst> spell_bursts;

    // The open screen's room, playing live: one decoder stepped at the
    // video's own pace, its frames written over the cached still so every
    // screen sees the fire move through the same lookup.
    struct RoomPlayer {
        std::string video;
        std::vector<std::byte> bytes;
        video::SmackerDecoder decoder;
        std::uint32_t frame = 0;
        std::uint64_t next_at = 0;
        bool ready = false;
    };
    RoomPlayer room;
    RoomPlayer movie;
    render::Texture movie_frame;


    // The campfire screen: R opens it, its buttons choose how long.
    bool rest_screen = start_rest;
    float step_timer = 0.0f;  // seconds to the next footfall

    // The opened chest's screen: which CHEST art shows and what was found.
    int chest_art = -1;
    std::string chest_note;

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
    // One bounding sphere per room/sector, in render space like face_bounds.
    // Empty when the map has no sector section; in that case sector culling is
    // skipped and the per-face sphere test stands on its own.
    std::vector<FaceBound> sector_bounds;
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
    // Bake a per-sector bounding sphere in render space, for coarse room
    // culling. A sector whose sphere leaves the view rejects all its faces at
    // once, before the per-face test runs. Re-run whenever the map changes.
    const auto bake_sector_bounds = [&] {
        sector_bounds.assign(session.blv.sectors.size(), {});
        for (std::size_t i = 0; i < session.blv.sectors.size(); ++i) {
            const auto& s = session.blv.sectors[i];
            const render::Vec3 corners[8] = {
                world::to_render_space(s.min_x, s.min_y, s.min_z),
                world::to_render_space(s.min_x, s.min_y, s.max_z),
                world::to_render_space(s.min_x, s.max_y, s.min_z),
                world::to_render_space(s.min_x, s.max_y, s.max_z),
                world::to_render_space(s.max_x, s.min_y, s.min_z),
                world::to_render_space(s.max_x, s.min_y, s.max_z),
                world::to_render_space(s.max_x, s.max_y, s.min_z),
                world::to_render_space(s.max_x, s.max_y, s.max_z),
            };
            render::Vec3 lo = corners[0];
            render::Vec3 hi = corners[0];
            for (int k = 1; k < 8; ++k) {
                lo.x = std::min(lo.x, corners[k].x);
                lo.y = std::min(lo.y, corners[k].y);
                lo.z = std::min(lo.z, corners[k].z);
                hi.x = std::max(hi.x, corners[k].x);
                hi.y = std::max(hi.y, corners[k].y);
                hi.z = std::max(hi.z, corners[k].z);
            }
            const render::Vec3 center{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f,
                                      (lo.z + hi.z) * 0.5f};
            float radius = 0.0f;
            for (int k = 0; k < 8; ++k) {
                const float dx = corners[k].x - center.x;
                const float dy = corners[k].y - center.y;
                const float dz = corners[k].z - center.z;
                radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            sector_bounds[i] = {center, radius};
        }
    };
    bake_lights();
    bake_sector_bounds();

    render::SceneRenderer scene(kWidth, kHeight);

    float fall_speed = 0.0f;
    int frame = 0;
    bool running = true;

    // Stand a door's vertices where its progress says, shared by the slide,
    // a thrown lever and loading a save. The caller rebuilds collision
    // after the last door it moves.
    const auto move_door = [&](world::MapDoor& door) { world::stand_door(session, door); };

    // Step the open interior one video frame when its time comes: the
    // frame lands in the cache under the video's name, so the service and
    // talk screens see the live room through their ordinary lookup, and
    // the frame's audio chunk feeds the mixer's room voice.
    const auto advance_room = [&](std::string_view video) {
        if (video.empty()) {
            return;
        }
        if (room.video != video) {
            room = {};
            room.video = std::string(video);
            room.ready = cache.interior_bytes(room.video, room.bytes) &&
                         video::SmackerDecoder::load(room.bytes, room.decoder) ==
                             video::SmackerError::None &&
                         room.decoder.info().frame_count > 0;
        }
        if (!room.ready || SDL_GetTicks() < room.next_at) {
            return;
        }
        std::span<const std::uint8_t> rgba;
        if (room.decoder.decode_frame_rgba(room.frame, rgba) == video::SmackerError::None) {
            std::vector<std::uint8_t> pixels(rgba.begin(), rgba.end());
            render::Texture picture;
            if (render::Texture::create(static_cast<std::uint16_t>(room.decoder.info().width),
                                        static_cast<std::uint16_t>(room.decoder.info().height),
                                        std::move(pixels), picture)) {
                cache.set_interior(room.video, std::move(picture));
            }
            video::SmackerAudioFrame chunk;
            // A one-frame capture must not open an audio device at all.
            if (mouse_look &&
                room.decoder.decode_audio(room.frame, 0, chunk) == video::SmackerError::None &&
                !chunk.samples.empty()) {
                const auto track = room.decoder.audio_info(0);
                ambient.play_room_chunk(chunk.samples.data(), chunk.samples.size(),
                                        static_cast<int>(track.sample_rate), track.stereo);
            }
        }
        room.frame = (room.frame + 1) % room.decoder.info().frame_count;
        const double fps = room.decoder.info().fps > 1.0 ? room.decoder.info().fps : 10.0;
        room.next_at = SDL_GetTicks() + static_cast<std::uint64_t>(1000.0 / fps);
    };

    // The voice a face speaks with: the archive's lines are named exactly
    // like the portrait frames — the sheet letter and a two-digit line,
    // then an a or b take. Which line fits which moment is the
    // executable's knowledge; the numbers picked here are the engine's
    // own, marked at each call. `inferred`
    const auto speak = [&](const game::Character& who, int line) {
        if (!mouse_look) {
            return;
        }
        const std::string name = game::portrait_entry(who.face, line);
        if (name.empty()) {
            return;
        }
        // The b take when the face has one for this line, else the a.
        const bool other = (SDL_GetTicks() / 731) % 2 == 1 && ambient.has(name + 'b');
        ambient.play_once(name + (other ? 'b' : 'a'));
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
        // The party's own buff array first: a spell that owns one of the
        // sixteen slots fills it with the power and the hours its case
        // computes, and nothing else needs to know. See src/game/buffs.hpp.
        if (const int slot = game::buff_slot_of_spell(spell.id); slot >= 0) {
            const int rank = game::rank_of(skill);
            const std::int64_t lasts =
                clock.minutes() +
                static_cast<std::int64_t>(std::max(1, skill)) * game::kSchoolBuffMinutesPerPoint;
            // A protection's power is one, two or three a point by rank;
            // the others carry the skill itself.
            const bool shields = slot <= static_cast<int>(game::PartyBuff::ProtectionFromPoison);
            party_buffs.cast(slot, lasts, shields ? game::fire_shield(skill, rank) : skill,
                             skill);
            return true;
        }
        if (spell.id == game::kSpellDayOfProtection) {
            // "Protects the party from everything": its case fills seven
            // slots in a row, at two, three or four times the skill plus ten.
            const int rank = game::rank_of(skill);
            const int power = game::day_of_the_gods(skill, rank);
            const std::int64_t lasts =
                clock.minutes() + static_cast<std::int64_t>(std::max(1, skill)) *
                                      game::by_school_rank(game::kDayOfTheGodsHours, rank) *
                                      game::kMinutesPerHour;
            for (const int slot : game::kDayOfProtectionSlots) {
                party_buffs.cast(slot, lasts, power, skill);
            }
            return true;
        }
        const data::SpellDuration duration = data::parse_spell_duration(spell, 0);
        if (duration.empty()) {
            return false;
        }
        std::int64_t until = clock.minutes() + duration.minutes(skill);
        const std::string name = data::cp1252_to_utf8(spell.name);
        if (spell.id == game::kSpellHaste) {
            // Haste's own case: sixty-four minutes and a minute a point,
            // three at master. The table rounds the base to an hour.
            until = clock.minutes() + game::haste_minutes(skill, game::rank_of(skill));
        }
        // The character's own slots, where the executable puts them: Haste
        // slot 2, Meditation 6 and 7, Power 10 and 11 — with the power the
        // ten-plus ladder computes rather than a bare flag.
        const int rank = game::rank_of(skill);
        // The pairs first: Meditation takes Intellect and Personality, Power
        // takes Might and Endurance, both on the ten-plus ladder.
        if (spell.id == game::kSpellMeditation || spell.id == 75) {
            const int power = game::ten_plus_ladder(skill, rank);
            for (const int slot :
                 spell.id == game::kSpellMeditation ? game::kMeditationSlots
                                                    : game::kPowerSlots) {
                who.buffs.cast(static_cast<std::size_t>(slot), until, power);
            }
            return true;
        }
        if (const int slot = game::character_slot_of_spell(spell.id); slot >= 0) {
            // A slot the stat getter reads carries the ten-plus ladder's own
            // number; the rest carry the skill itself.
            const bool moves_a_stat = slot >= 4;
            who.buffs.cast(static_cast<std::size_t>(slot), until,
                           moves_a_stat ? game::ten_plus_ladder(skill, rank) : skill);
            if (spell.id != game::kSpellHaste) {
                return true;
            }
        }
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
    // What a healing cast gives. Three of Body's spells have their amounts
    // read straight out of the executable's spell switch — First Aid's flat
    // 5/7/10, Cure Wounds' 2 a point plus five, Power Cure's 2 a point plus
    // ten over the whole party — and everything else still answers to the
    // table's prose. See src/game/body_magic.hpp.
    const auto heal_amount = [](const data::SpellStatsEntry& spell,
                                const data::SpellEffect& effect, int points, int rank) {
        const int traced = game::traced_heal(spell.id, points, rank);
        return traced > 0 ? traced : std::max(1, effect.heal.low);
    };
    const auto pour_heal = [&party](int spell_id, int amount, std::size_t worst) -> std::string {
        if (!game::heal_reaches_party(spell_id)) {
            party[worst].hit_points =
                std::min(party[worst].max_hit_points, party[worst].hit_points + amount);
            return party[worst].name;
        }
        for (auto& member : party) {
            if (member.hit_points > 0) {
                member.hit_points =
                    std::min(member.max_hit_points, member.hit_points + amount);
            }
        }
        return "the whole party";
    };

    const auto cure_with = [&](const data::SpellStatsEntry& spell,
                               int school_points) -> std::string {
        const data::SpellCure cure = data::parse_spell_cure(spell);
        if (cure.empty()) {
            return {};
        }
        // "If you cast this spell in time": the window is the executable's
        // own ladder, three minutes a point at normal rank, three hours at
        // expert and three days at master. Past it the prose sends you to a
        // temple, and so does this. See src/game/body_magic.hpp.
        const std::int64_t window = game::cure_window_minutes(
            school_points, game::rank_of(school_points), spell.id);
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
            points = game::skill_points(it->second);
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
            points = game::skill_points(it->second);
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
        // A point of Identify Item is worth 120% of itself, by the
        // executable's own weight for skill id 21.
        return game::weighted_identify(best) >= row.id_rep_st;
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
            speak(party[0], 30);  // line 30: the finder's word
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
    // One map's memory between visits in a session.
    struct MapMemory {
        std::set<int> opened_chests;
        std::vector<std::uint32_t> open_doors;
        std::vector<std::size_t> dead;
        std::int64_t remembered_day = 0;
    };
    std::map<std::string, MapMemory> map_memory;

    const auto open_map = [&](const std::string& name) -> bool {
        // What the map being left will remember: the fallen, the opened
        // and the thrown, kept per file the way the original's state
        // files kept them, and forgotten after its own Refil Days.
        if (!session.file_name.empty()) {
            MapMemory& memory = map_memory[session.file_name];
            memory.opened_chests = opened_chests;
            memory.open_doors.clear();
            for (const auto& door : session.doors) {
                if (door.open) {
                    memory.open_doors.push_back(door.id);
                }
            }
            memory.dead.clear();
            for (std::size_t i = 0; i < session.actors.size(); ++i) {
                if (!battle.alive(i)) {
                    memory.dead.push_back(i);
                }
            }
            memory.remembered_day = clock.day();
        }
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
        bake_sector_bounds();
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
        map_colors_ready = false;
        show_map = false;
        fall_speed = 0.0f;
        // A door whose attribute word carries bit 0 ships open: the
        // original's load loop at 0x45556e tests exactly that bit, sets
        // the state word to open and the travel to full (0x3c00), then
        // rewrites the attribute to 2. Traced; see
        // docs/formats/event-tables.md.
        {
            bool starts_open = false;
            for (auto& door : session.doors) {
                if ((door.attributes & 1) != 0) {
                    door.open = true;
                    door.progress = 1.0f;
                    move_door(door);
                    starts_open = true;
                }
            }
            if (starts_open) {
                world::rebuild_indoor_collision(session);
            }
        }
        // And what this one remembers, if its Refil Days have not run out
        // (a map that never refills remembers forever).
        if (const auto it = map_memory.find(session.file_name); it != map_memory.end()) {
            const bool expired =
                session.refill_days > 0 &&
                clock.day() >= it->second.remembered_day + session.refill_days;
            if (expired) {
                map_memory.erase(it);
            } else {
                opened_chests = it->second.opened_chests;
                bool doors_restored = false;
                for (const std::uint32_t id : it->second.open_doors) {
                    for (auto& door : session.doors) {
                        if (door.id == id) {
                            door.open = true;
                            door.progress = 1.0f;
                            move_door(door);
                            doors_restored = true;
                        }
                    }
                }
                if (doors_restored) {
                    world::rebuild_indoor_collision(session);
                }
                for (const std::size_t i : it->second.dead) {
                    battle.kill(i);
                    if (i < shown_kind.size()) {
                        shown_kind[i] = world::MonsterAnimation::Death;
                    }
                }
            }
        }
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
        // A monster's cast flies its school's own bolt and speaks with the
        // spell's own sound — the same joins the party's casting uses.
        for (const auto& [caster, spell_id] : battle.take_casts()) {
            if (caster >= session.actors.size()) {
                continue;
            }
            ambient.play_spell(spell_id);
            if (const auto* spell = spell_stats.at(static_cast<std::size_t>(spell_id))) {
                game::ActiveLaunch bolt;
                bolt.animation = game::spell_sprite_group(spell->school, spell->number);
                bolt.position = session.actors[caster].position;
                bolt.position.y += 32.0f;
                bolt.target = camera.position;
                launches.push_back(std::move(bolt));
            }
        }
        for (const int killed : battle.take_kills()) {
            kills_this_month.insert(killed);
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
            // Mouse coordinates arrive in window space; the zones are laid
            // out in the logical 640x480.
            SDL_ConvertEventToRenderCoordinates(sdl_renderer, &event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (!movie_queue.empty() &&
                       (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
                // Any key sends the reel forward.
                movie_queue.erase(movie_queue.begin());
                movie = {};
                ambient.stop_room();
            } else if (at_title &&
                       (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
                // The four plates in a row along the painting's foot.
                int chosen = -1;
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    const auto key = event.key.key;
                    chosen = key == SDLK_N        ? 0
                             : key == SDLK_L      ? 1
                             : key == SDLK_C      ? 2
                             : key == SDLK_ESCAPE ? 3
                                                  : -1;
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    const int mx = static_cast<int>(event.button.x);
                    const int my = static_cast<int>(event.button.y);
                    if (my >= 424 && my < 469 && mx >= 20 && (mx - 20) % 152 < 135) {
                        chosen = (mx - 20) / 152;
                    }
                }
                const auto leave_title = [&] {
                    at_title = false;
                    if (mouse_look && !cursor_free) {
                        SDL_SetWindowRelativeMouseMode(window, true);
                    }
                };
                if (chosen >= 0) {
                    ambient.play_once("ClickStart");
                }
                if (chosen == 0) {
                    leave_title();
                    creating = true;
                } else if (chosen == 1) {
                    leave_title();
                    SDL_Event synthetic{};
                    synthetic.type = SDL_EVENT_KEY_DOWN;
                    synthetic.key.key = SDLK_F9;
                    SDL_PushEvent(&synthetic);
                } else if (chosen == 2) {
                    title_credits = !title_credits;
                    movie_queue = {"credits"};
                } else if (chosen == 3) {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && arena_rank < 0 &&
                       session.file_name == "zarena.blv" && event.key.key >= SDLK_1 &&
                       event.key.key <= SDLK_4 && shown_member < 0 && shown_pack < 0 &&
                       open_shop < 0 && !battle.anything_alive()) {
                // The challenge taken at the gate: the rank picks a level
                // band and a crowd, both the engine's numbers.
                arena_rank = static_cast<int>(event.key.key - SDLK_1);
                const int low = 3 + arena_rank * 12;
                const int high = 15 + arena_rank * 20;
                std::vector<int> pool;
                for (const auto& row : monster_stats.entries()) {
                    if (row.level >= low && row.level <= high && row.hostility > 0) {
                        pool.push_back(row.id);
                    }
                }
                const int crowd = 3 + arena_rank * 2;
                bool any = false;
                for (int i = 0; i < crowd && !pool.empty(); ++i) {
                    const int id = pool[arena_random.next() % pool.size()];
                    const auto [dx, dy] = world::spawn_offset(i, crowd);
                    const render::Vec3 at{camera.position.x + dx * 4.0f, camera.position.y,
                                          camera.position.z + dy * 4.0f};
                    any = world::summon_actor(monster_stats, cache, id, at, session) || any;
                }
                if (any) {
                    battle.recruit(session, monster_stats);
                    mob.recruit(session, monster_stats);
                    shown_kind.resize(session.actors.size(), world::MonsterAnimation::Stand);
                    shown_animation.resize(session.actors.size());
                    pick_up_message = "The crowd roars";
                    pick_up_shown = SDL_GetTicks();
                } else {
                    arena_rank = -1;
                }
            } else if ((show_quickref || show_options) &&
                       (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
                show_quickref = false;
                show_options = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && chest_art >= 0) {
                chest_art = -1;
            } else if (event.type == SDL_EVENT_KEY_DOWN && book_member >= 0) {
                // The book holds the keys while it is open.
                const auto key = event.key.key;
                auto& who = party[static_cast<std::size_t>(book_member)];
                const auto schools = schools_known(who);
                if (key == SDLK_ESCAPE || key == SDLK_B) {
                    book_member = -1;
                } else if (key >= SDLK_1 && key <= SDLK_4) {
                    book_member = static_cast<int>(key - SDLK_1);
                    book_school = 0;
                    book_pick = 0;
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    const int count = static_cast<int>(schools.size());
                    if (count > 0) {
                        book_school = (book_school + (key == SDLK_LEFT ? count - 1 : 1)) % count;
                        book_pick = 0;
                    }
                } else if (key == SDLK_UP || key == SDLK_DOWN) {
                    int count = 0;
                    if (!schools.empty()) {
                        const auto school = schools[static_cast<std::size_t>(
                            std::clamp(book_school, 0, static_cast<int>(schools.size()) - 1))];
                        for (const int id : who.known_spells) {
                            const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                            count += spell != nullptr && spell->school == school ? 1 : 0;
                        }
                    }
                    if (count > 0) {
                        book_pick = (book_pick + (key == SDLK_UP ? count - 1 : 1)) % count;
                    }
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (!schools.empty()) {
                        const auto school = schools[static_cast<std::size_t>(
                            std::clamp(book_school, 0, static_cast<int>(schools.size()) - 1))];
                        std::vector<int> page;
                        for (const int id : who.known_spells) {
                            const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                            if (spell != nullptr && spell->school == school) {
                                page.push_back(id);
                            }
                        }
                        if (!page.empty()) {
                            const auto at = static_cast<std::size_t>(
                                std::clamp(book_pick, 0, static_cast<int>(page.size()) - 1));
                            readied[static_cast<std::size_t>(book_member)] = page[at];
                            const auto* spell = spell_stats.at(static_cast<std::size_t>(page[at]));
                            pick_up_message = who.name + " readies " +
                                              data::cp1252_to_utf8(spell->name);
                            pick_up_shown = SDL_GetTicks();
                            book_member = -1;
                        }
                    }
                }
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
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_M &&
                       shown_member < 0 && shown_pack < 0 && open_shop < 0 && !creating) {
                cursor_free = !cursor_free;
                if (mouse_look) {
                    SDL_SetWindowRelativeMouseMode(window, !cursor_free);
                }
                pick_up_message = cursor_free ? "The cursor is yours; M returns it to the view"
                                              : "";
                pick_up_shown = SDL_GetTicks();
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                // Fullscreen, with the integer presentation keeping the
                // pixels square either way.
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen);
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
                            points = game::skill_points(it->second);
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
                state.readied = readied;
                state.turn_based = turn_based;
                state.hourglass_turn = hourglass_turn;
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
                state.autonotes = script_state.autonotes;
                // The maps the party has been away from, plus the one
                // underfoot: a save should find a cleared dungeon cleared.
                state.remembered.clear();
                for (const auto& [file, memory] : map_memory) {
                    state.remembered.push_back({file, memory.remembered_day,
                                                memory.opened_chests, memory.open_doors,
                                                memory.dead});
                }
                {
                    game::SaveState::RememberedMap here;
                    here.file = session.file_name;
                    here.day = clock.day();
                    here.opened_chests = opened_chests;
                    for (const auto& door : session.doors) {
                        if (door.open) {
                            here.open_doors.push_back(door.id);
                        }
                    }
                    for (std::size_t i = 0; i < session.actors.size(); ++i) {
                        if (!battle.alive(i)) {
                            here.dead.push_back(i);
                        }
                    }
                    std::erase_if(state.remembered, [&](const auto& m) {
                        return m.file == here.file;
                    });
                    state.remembered.push_back(std::move(here));
                }
                state.party = party;
                state.party_buffs = party_buffs;
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
                // The maps' memories are restored before the map opens, so
                // the one being entered finds its own dead already down.
                const bool parsed = file.good() && game::parse_save(buffer.str(), state);
                if (parsed) {
                    map_memory.clear();
                    for (const auto& map : state.remembered) {
                        map_memory[map.file] = {map.opened_chests, map.open_doors, map.dead,
                                                map.day};
                    }
                }
                if (!parsed || !open_map(state.map_file)) {
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
                    readied = state.readied;
                    turn_based = state.turn_based;
                    hourglass_turn = state.hourglass_turn;
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
                    script_state.autonotes = state.autonotes;
                    party = state.party;
                    party_buffs = state.party_buffs;
                    for (std::size_t i = 0; i < packs.size(); ++i) {
                        packs[i].clear();
                        for (const auto& item : state.packs[i]) {
                            (void)packs[i].place(item);
                        }
                    }
                    opened_chests =
                        std::set<int>(state.opened_chests.begin(), state.opened_chests.end());
                    // The save's open list is the whole door state, so
                    // first shut everything — including doors that start
                    // open by their attribute bit — then open the listed.
                    bool doors_moved = false;
                    for (auto& door : session.doors) {
                        door.open = false;
                        door.progress = 0.0f;
                        move_door(door);
                        doors_moved = true;
                    }
                    for (const std::uint32_t id : state.open_doors) {
                        for (auto& door : session.doors) {
                            if (door.id == id) {
                                door.open = true;
                                door.progress = 1.0f;
                                move_door(door);
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
                // What the smith asks, less what the party's own Repair
                // Item skill saves — weighted 120% by the executable's
                // table for skill id 23, one percent off the bill a point.
                int repair_points = 0;
                for (const auto& member : party) {
                    if (const auto it = member.skills.find("Repair");
                        it != member.skills.end()) {
                        repair_points = std::max(repair_points, it->second);
                    }
                }
                const int repair_off =
                    std::min(50, game::weighted_repair(repair_points));
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
                bill -= bill * repair_off / 100;
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
            } else if (event.type == SDL_EVENT_KEY_DOWN && show_journal &&
                       (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                journal_page = 1 - journal_page;
            } else if (event.type == SDL_EVENT_KEY_DOWN && shown_member >= 0 &&
                       shown_pack < 0 && open_shop < 0 &&
                       (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
                sheet_page =
                    (sheet_page + (event.key.key == SDLK_LEFT ? 3 : 1)) % 4;
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       talking_to < 0 && shown_pack < 0 &&
                       (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                       (shops_here[static_cast<std::size_t>(open_shop)]->type == "Town Hall" ||
                        shops_here[static_cast<std::size_t>(open_shop)]->type ==
                            "City Council")) {
                if (const auto* head = bounty_of();
                    head != nullptr && bounty_month_paid != bounty_month() &&
                    kills_this_month.contains(head->id)) {
                    const int purse = head->level * 100;
                    gold += purse;
                    bounty_month_paid = bounty_month();
                    script_state.awards.insert(81);
                    script_state.variables[239] += 1;
                    shop_said = "The clerk counts out " + std::to_string(purse) +
                                " gold and records the deed.";
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       talking_to < 0 && shown_pack < 0 &&
                       (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                       !entrance_map_of(*shops_here[static_cast<std::size_t>(open_shop)])
                            .empty()) {
                // Through the mouth of the dungeon.
                const std::string inside =
                    entrance_map_of(*shops_here[static_cast<std::size_t>(open_shop)]);
                open_shop = -1;
                shop_said.clear();
                ambient.stop_room();
                if (open_map(inside)) {
                    pick_up_message = "The party descends";
                    pick_up_shown = SDL_GetTicks();
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && open_shop >= 0 &&
                       talking_to < 0 && shown_pack < 0 &&
                       (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT ||
                        event.key.key == SDLK_UP || event.key.key == SDLK_DOWN ||
                        event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                // The shelf browsed by eye: arrows walk the grid, Enter
                // buys what the border holds — through the same digit path.
                const int count = static_cast<int>(std::min<std::size_t>(shop_stock.size(), 9));
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    if (shop_pick >= 0 && shop_pick < count) {
                        SDL_Event synthetic{};
                        synthetic.type = SDL_EVENT_KEY_DOWN;
                        synthetic.key.key = static_cast<SDL_Keycode>(SDLK_1 + shop_pick);
                        SDL_PushEvent(&synthetic);
                    }
                } else if (count > 0) {
                    const int step = event.key.key == SDLK_LEFT    ? -1
                                     : event.key.key == SDLK_RIGHT ? 1
                                     : event.key.key == SDLK_UP    ? -3
                                                                   : 3;
                    shop_pick = ((shop_pick + step) % count + count) % count;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key >= SDLK_1 &&
                       event.key.key <= SDLK_9) {
                const int chosen = static_cast<int>(event.key.key - SDLK_1);
                // Held shift over a numbered skill buys its next rung from
                // the teacher, at the teacher's own price — 2000 gold for
                // expert, 5000 for master, and the bits it sets are the ones
                // `0x4969e4` sets. The rung is bought with the party's purse,
                // not with the sheet's skill points, which buy only the
                // number beneath it.
                if (const bool shifted = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    shifted && shown_member >= 0 && chosen >= 4 && chosen < 9) {
                    auto& who = party[static_cast<std::size_t>(shown_member)];
                    int index = chosen - 4;
                    for (const auto& [skill, packed] : who.skills) {
                        if (index-- != 0) {
                            continue;
                        }
                        const int slot = game::skill_id(skill);
                        const int want = game::skill_rank(packed) + 1;
                        switch (game::buy_rank(who, slot, want, gold)) {
                            case game::TeachRefusal::None:
                                shop_said = who.name + " is taught to " +
                                            std::string(game::kRankNames[
                                                static_cast<std::size_t>(want)]) +
                                            " in " + skill;
                                break;
                            case game::TeachRefusal::TooPoor:
                                shop_said = "That costs " +
                                            std::to_string(game::teach_price(want)) + " gold.";
                                break;
                            case game::TeachRefusal::NotNextRung:
                                shop_said = who.name + " has nothing left to learn in " + skill;
                                break;
                            default:
                                shop_said = "No one here teaches " + skill + " to a " +
                                            who.class_name;
                                break;
                        }
                        break;
                    }
                    break;
                }
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
                        // The class tables carry what the level is worth,
                        // so the prose no longer needs reading for it.
                        game::train(who);
                        shop_said = who.name + " reaches level " + std::to_string(who.level) + ".";
                        speak(who, 20);  // line 20: the trained word
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
                        // The training routine's own two refusals: sixty is
                        // the ceiling, and a class may never be taught what
                        // its row in `0x4c2694` zeroes.
                        const int cost = game::raise_cost(points);
                        const bool allowed =
                            game::skill_points(points) < game::kSkillPointCap &&
                            game::class_may_learn(game::class_id(who.class_name),
                                                  game::skill_id(skill));
                        if (allowed && who.skill_points >= cost) {
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
                        data::SpellRange wand_flat;
                        data::SpellRange wand_scaled;
                        const int wand_skill = spell_skill_of(party[who], *spell);
                        const bool wand_traced = game::traced_damage_ranges(
                            spell_id, wand_skill, wand_flat, wand_scaled);
                        what = battle.smite(target,
                                            wand_traced ? wand_flat : effect.damage,
                                            wand_traced ? wand_scaled : effect.damage_per_skill,
                                            wand_skill, spell->element,
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
                        if (!effect.heal.empty() ||
                            game::traced_heal(spell_id, 0, 0) > 0) {
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
                            speak(party[worst], 24);  // line 24: the drink's word
                            // A scroll casts at no skill and normal rank, so
                            // the traced spells give their floor.
                            const std::string mended =
                                pour_heal(spell_id, heal_amount(*spell, effect, 0, 0), worst);
                            what = party[who].name + " reads " +
                                   data::cp1252_to_utf8(row->name) + ": " + mended +
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
                            // The executable's own dice for this spell, when
                            // it has a case; the prose only when it does not.
                            data::SpellRange traced_flat;
                            data::SpellRange traced_scaled;
                            const int held_skill = spell_skill_of(party[who], *spell);
                            const bool traced = game::traced_damage_ranges(
                                spell_id, held_skill, traced_flat, traced_scaled);
                            what = battle.smite(target,
                                                traced ? traced_flat : effect.damage,
                                                traced ? traced_scaled : effect.damage_per_skill,
                                                held_skill,
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
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_B &&
                       shown_member < 0 && shown_pack < 0 && open_shop < 0 && !creating &&
                       !show_journal) {
                // Open the book of the first member with anything in it.
                for (std::size_t i = 0; i < party.size(); ++i) {
                    if (!party[i].known_spells.empty()) {
                        book_member = static_cast<int>(i);
                        book_school = 0;
                        book_pick = 0;
                        break;
                    }
                }
                if (book_member < 0) {
                    pick_up_message = "Nobody has a spell to their name";
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
                // A spell reaches the missile band, the same range the
                // monsters' own shots use.
                const std::size_t target = game::aimed_actor(
                    session, battle, camera.position, camera.forward(), game::kMissileRange);
                // A readied spell outranks the heuristic: the first member
                // holding one casts exactly it, the player's own choice.
                for (std::size_t i = 0; i < party.size() && !cast; ++i) {
                    const int id = readied[i];
                    auto& caster = party[i];
                    const auto* spell =
                        id > 0 ? spell_stats.at(static_cast<std::size_t>(id)) : nullptr;
                    if (spell == nullptr || !caster.can_act()) {
                        continue;
                    }
                    const int points = spell_skill_of(caster, *spell);
                    const int rank = game::rank_of(points);
                    const int cost = rank >= 2   ? spell->cost_master
                                     : rank == 1 ? spell->cost_expert
                                                 : spell->cost_normal;
                    if (caster.spell_points < cost) {
                        pick_up_message = caster.name + " lacks the spell points for " +
                                          data::cp1252_to_utf8(spell->name);
                        pick_up_shown = SDL_GetTicks();
                        cast = true;
                        break;
                    }
                    // The executable keeps its own list of which spells are
                    // thrown at the world; one of those with nothing in
                    // reach is not cast at all, and costs nothing.
                    if (game::spell_is_aimed(spell->id) && target == game::kNoActor) {
                        pick_up_message = "Nothing in reach to cast at";
                        pick_up_shown = SDL_GetTicks();
                        cast = true;
                        break;
                    }
                    const data::SpellEffect effect = data::parse_spell_effect(*spell, rank);
                    if (!effect.heal.empty() || game::traced_heal(spell->id, points, rank) > 0) {
                        caster.spell_points -= cost;
                        ambient.play_spell(spell->id);
                        const std::string mended = pour_heal(
                            spell->id, heal_amount(*spell, effect, points, rank), worst);
                        pick_up_message = caster.name + " casts " +
                                          data::cp1252_to_utf8(spell->name) + " on " + mended;
                    } else if (!effect.damage.empty() || !effect.damage_per_skill.empty()) {
                        if (target == game::kNoActor) {
                            pick_up_message = "Nothing in reach to cast at";
                            pick_up_shown = SDL_GetTicks();
                            cast = true;
                            break;
                        }
                        caster.spell_points -= cost;
                        ambient.play_spell(spell->id);
                        SpellShot shot;
                        shot.target = target;
                        shot.flat = effect.damage;
                        shot.per_skill = effect.damage_per_skill;
                        // The executable's own dice, where the spell has a case;
                        // the prose stands where it has none.
                        if (data::SpellRange f, p;
                            game::traced_damage_ranges(spell->id, points, f, p)) {
                            shot.flat = f;
                            shot.per_skill = p;
                        }
                        shot.skill = points;
                        shot.element = spell->element;
                        shot.reach = effect.reach;
                        shot.caster = caster.name;
                        shot.flight.animation =
                            game::spell_sprite_group(spell->school, spell->number);
                        const std::string burst =
                            game::spell_sprite_group(spell->school, spell->number, true);
                        if (!session.sprite_frames.group(burst).empty()) {
                            shot.burst = burst;
                        }
                        shot.flight.position = camera.position;
                        shot.flight.target = session.actors[target].position;
                        shot.flight.target.y += 32.0f;
                        pick_up_message =
                            caster.name + " casts " + data::cp1252_to_utf8(spell->name);
                        spell_shots.push_back(std::move(shot));
                    } else if (std::string lifted = cure_with(*spell, points); !lifted.empty()) {
                        caster.spell_points -= cost;
                        ambient.play_spell(spell->id);
                        pick_up_message = caster.name + " casts: " + lifted;
                    } else {
                        pick_up_message = data::cp1252_to_utf8(spell->name) +
                                          " has nothing to do here";
                    }
                    pick_up_shown = SDL_GetTicks();
                    cast = true;
                }
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
                        const int held = spell_skill_of(caster, *best);
                        const std::string mended =
                            pour_heal(best->id,
                                      heal_amount(*best, best_effect, held, game::rank_of(held)),
                                      worst);
                        pick_up_message = caster.name + " casts " +
                                          data::cp1252_to_utf8(best->name) + " on " + mended;
                    } else if (target != game::kNoActor) {
                        caster.spell_points -= best->cost_normal;
                        ambient.play_spell(best->id);
                        // The bolt carries the blow; it lands on arrival.
                        SpellShot shot;
                        shot.target = target;
                        shot.flat = best_effect.damage;
                        shot.per_skill = best_effect.damage_per_skill;
                        if (data::SpellRange f, p;
                            game::traced_damage_ranges(best->id, spell_skill_of(caster, *best), f,
                                                       p)) {
                            shot.flat = f;
                            shot.per_skill = p;
                        }
                        shot.skill = spell_skill_of(caster, *best);
                        shot.element = best->element;
                        shot.reach = best_effect.reach;
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
                       shown_member < 0 && shown_pack < 0 && open_shop < 0) {
                rest_screen = true;
            } else if (rest_screen &&
                       (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
                // The camp's own buttons: rest and heal, sleep to dawn, or
                // sit an hour out; the exit plate folds the blanket.
                int chosen = -1;
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    const auto key = event.key.key;
                    chosen = key >= SDLK_1 && key <= SDLK_3 ? static_cast<int>(key - SDLK_1)
                             : key == SDLK_ESCAPE           ? 3
                                                            : -1;
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    const int mx = static_cast<int>(event.button.x);
                    const int my = static_cast<int>(event.button.y);
                    if (mx >= 65 && mx < 219) {
                        if (my >= 241 && my < 268) {
                            chosen = 0;
                        } else if (my >= 271 && my < 298) {
                            chosen = 1;
                        } else if (my >= 301 && my < 328) {
                            chosen = 2;
                        }
                    }
                    if (mx >= 285 && mx < 439 && my >= 308 && my < 345) {
                        chosen = 3;
                    }
                }
                if (chosen >= 0) {
                    ambient.play_once("ClickIn");
                }
                if (chosen == 0 || chosen == 1) {
                    speak(party[0], 21);  // line 21: the goodnight
                }
                if (chosen == 0) {
                    want_rest = true;
                    rest_screen = false;
                } else if (chosen == 1) {
                    // Sleep so the eight hours of rest end at five in the
                    // morning: wait out the difference first.
                    const int now_minute =
                        static_cast<int>(clock.minutes() % game::kMinutesPerDay);
                    const int dawn = 5 * game::kMinutesPerHour;
                    const int wait =
                        ((dawn - now_minute - 8 * game::kMinutesPerHour) %
                             game::kMinutesPerDay +
                         game::kMinutesPerDay) %
                        game::kMinutesPerDay;
                    clock.advance_seconds(static_cast<float>(wait) * 60.0f);
                    want_rest = true;
                    rest_screen = false;
                } else if (chosen == 2) {
                    clock.advance_seconds(60.0f * 60.0f);
                    pick_up_message = "An hour passes at the fire";
                    pick_up_shown = SDL_GetTicks();
                } else if (chosen == 3) {
                    rest_screen = false;
                }
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_look && !cursor_free) {
                camera.yaw += event.motion.xrel * game::kMouseSensitivity;
                camera.pitch -= event.motion.yrel * game::kMouseSensitivity;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && cursor_free &&
                       event.button.button == SDL_BUTTON_LEFT) {
                // The frame's own buttons, by their painted places: the four
                // books, the four medallions, and the portraits' oval seats.
                // The zones are read off the panels; which screen each book
                // opens follows the shelf's pictures — sword for quests,
                // quill for notes, globe for maps, key for the calendar.
                const int mx = static_cast<int>(event.button.x);
                const int my = static_cast<int>(event.button.y);
                const auto push_key = [](SDL_Keycode key) {
                    SDL_Event synthetic{};
                    synthetic.type = SDL_EVENT_KEY_DOWN;
                    synthetic.key.key = key;
                    SDL_PushEvent(&synthetic);
                };
                if ((mx >= 478 && mx < 634 && my >= 115 && my < 325) ||
                    (my >= 361 && my < 440 && mx >= 22 && mx < 440)) {
                    ambient.play_once("ClickIn");
                }
                if (mx >= 478 && mx < 634 && my >= 115 && my < 200) {
                    const int book = (mx - 478) / 39;
                    if (book == 0) {
                        push_key(SDLK_J);
                    } else if (book == 1) {
                        push_key(SDLK_TAB);
                    } else if (book == 2) {
                        show_map = !show_map;
                    } else {
                        show_calendar = !show_calendar;
                    }
                } else if (mx >= 478 && mx < 634 && my >= 252 && my < 325) {
                    const int seal = (mx - 478) / 39;
                    if (seal == 0) {
                        push_key(SDLK_H);
                    } else if (seal == 1) {
                        push_key(SDLK_R);
                    } else if (seal == 2) {
                        show_quickref = true;
                    } else {
                        show_options = true;
                    }
                } else if (my >= 361 && my < 440 && mx >= 22 && mx < 440) {
                    const int seat = (mx - 22) / 113;
                    if (seat >= 0 && seat < 4 && (mx - 22) % 113 < 59) {
                        shown_member = shown_member == seat ? -1 : seat;
                        shown_pack = -1;
                    }
                }
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
        // AlwaysRun from the install's own ini: shift walks instead.
        const bool shifted = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        const bool running_now = ini_always_run ? !shifted : shifted;
        in.speed = running_now ? 1200.0f : 400.0f;

        // Footfalls: the ground's own Walk/Run sound at a walking cadence
        // while the party moves in the world. The cadence is the engine's.
        if (!at_title && !creating && open_shop < 0 && shown_member < 0 && shown_pack < 0 &&
            book_member < 0 && !rest_screen && mouse_look &&
            (in.forward || in.back || in.left || in.right)) {
            step_timer -= in.dt > 0.0f ? in.dt : 0.0f;
            if (step_timer <= 0.0f) {
                const bool running = running_now;
                ambient.play_step(std::string(running ? "Run" : "Walk") +
                                  std::string(session.ground_at(camera.position.x,
                                                                camera.position.z)));
                step_timer = running ? 0.34f : 0.52f;
            }
        } else {
            step_timer = 0.0f;
        }

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
            // The day's sky, rolled the original's way when the day turns.
            if (clock.day() != sky_day) {
                sky_day = clock.day();
                static constexpr std::array<int, 9> kFair{1, 3, 6, 7, 8, 9, 12, 14, 15};
                static constexpr std::array<int, 7> kOther{2, 5, 10, 13, 16, 18, 19};
                const int pick = static_cast<int>(misc_random.next() % 100) < 20
                                     ? kOther[misc_random.next() % kOther.size()]
                                     : kFair[misc_random.next() % kFair.size()];
                char rolled_name[8];
                std::snprintf(rolled_name, sizeof(rolled_name), "sky%02d", pick);
                sky_today = rolled_name;
            }
            draw_sky(scene, cache, session, camera.yaw, camera.pitch, game::light_level(clock),
                     sky_today);
            draw_outdoor(scene, session, cache, game::sun_direction(clock),
                         game::light_level(clock));
        } else {
            // "Increases the radius of light": this renderer has no radius,
            // so the lamp itself brightens for the written hours. `inferred`
            draw_indoor(scene, session, cache, lamp,
                        clock.minutes() < torch_until ? 1.45f : 1.0f, &face_light,
                        [&](std::size_t index) {
                            // Coarse: reject the whole room at once. A face
                            // without a sector (or with no sector data) falls
                            // through to the per-face test below.
                            if (index < session.blv.face_sector.size()) {
                                const std::uint16_t sec = session.blv.face_sector[index];
                                if (sec != world::kBlvFaceNoSector && sec < sector_bounds.size() &&
                                    sector_bounds[sec].radius > 0.0f) {
                                    if (!scene.might_see(sector_bounds[sec].center,
                                                         sector_bounds[sec].radius)) {
                                        return true;
                                    }
                                }
                            }
                            // Fine: per-face bounding sphere.
                            return index < face_bounds.size() &&
                                   face_bounds[index].radius > 0.0f &&
                                   !scene.might_see(face_bounds[index].center,
                                                    face_bounds[index].radius);
                        });
        }
        // Real time flows every frame; turn-based time flows only when a
        // round is owed, one quantum at a time.
        const float sim_dt =
            at_title ? 0.0f : (turn_based ? (pending_round ? kRoundSeconds : 0.0f) : in.dt);
        if (turn_based && pending_round) {
            ++hourglass_turn;
        }
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
            // Doors travel between their stations at the file's own
            // open and close speeds, read as world units a second;
            // collision follows the geometry while anything slides.
            bool doors_sliding = false;
            for (auto& door : session.doors) {
                const float target = door.open ? 1.0f : 0.0f;
                if (door.progress == target || door.distance <= 0) {
                    continue;
                }
                const int speed = door.open ? door.open_speed : door.close_speed;
                const float rate = speed > 0 ? static_cast<float>(speed) /
                                                   static_cast<float>(door.distance)
                                             : 2.0f;
                const float step = rate * sim_dt;
                door.progress = door.progress < target
                                    ? std::min(target, door.progress + step)
                                    : std::max(target, door.progress - step);
                move_door(door);
                doors_sliding = true;
            }
            if (doors_sliding) {
                world::rebuild_indoor_collision(session);
            }
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
                if (std::string blow = battle.smite_area(
                        shot.target, shot.flat, shot.per_skill, shot.skill, shot.element,
                        shot.caster, shot.reach, session, monster_stats, item_stats,
                        random_items, standard_bonuses, special_bonuses);
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
            // The experience is banked and no more: the executable raises
            // no level by itself, so it is the training hall that spends it.
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
            std::array<int, game::kAttributeCount> slot_seen{};
            party[i].gear_resistances.fill(0);
            // The party's own protections stand on top of whatever the
            // character carries: the five slots the stat getter reads, in
            // MONSTERS.TXT's own column order.
            for (std::size_t r = 0; r < data::kResistanceCount; ++r) {
                party[i].gear_resistances[r] += party_buffs.resistance(r, clock.minutes());
            }
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
                    // And the character's own buff slot for that attribute,
                    // if a spell has filled it.
                    if (const int slot = game::buff_slot_for_stat(static_cast<int>(a));
                        slot >= 0 && slot_seen[a] == 0) {
                        party[i].gear_attributes[a] += party[i].buffs.power(
                            static_cast<std::size_t>(slot), clock.minutes());
                        slot_seen[a] = 1;
                    }
                    // And what the executable's own walk over the special
                    // gives that stat, which the prose does not say.
                    party[i].gear_attributes[a] += game::special_stat_bonus(
                        party[i].worn_special[slot], static_cast<int>(a));
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
                    points += game::skill_points(it->second);
                }
                skilled_armor += power_of_skill(row->skill_group, points).armor;
            }
            // "Increases the armor class of a character by 5 + 1 point per
            // point of skill in Earth Magic" — Stone Skin, out of slot 4.
            int warded = 0;
            if (const int stone =
                    party[i].buffs.power(game::CharacterBuff::StoneSkin, clock.minutes());
                stone > 0) {
                warded = game::kBlessBase + stone;
            }
            party[i].armor_class =
                game::attribute_bonus(party[i].attribute(game::Attribute::Speed)) +
                game::armour_of(party[i], item_stats) + party[i].temp_armor + skilled_armor +
                gear_armor + warded;
        }

        // An hour awake is an hour worn: the counter climbs, and past its
        // second hour it lays Weak on anyone not already afflicted.
        if (const std::int64_t hour_now = clock.minutes() / game::kMinutesPerHour;
            hour_now != fatigue_hour) {
            hours_awake += static_cast<int>(hour_now - fatigue_hour);
            fatigue_hour = hour_now;
            if (hours_awake >= game::kFatigueWeakAfterHours) {
                for (auto& who : party) {
                    if (who.affliction.empty() && who.hit_points > 0) {
                        who.affliction = "Weak";
                        who.affliction_minute = clock.minutes();
                    }
                }
            }
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
                // A rest ends every spell: the original's own rest routine
                // walks the party's sixteen slots and each character's
                // sixteen and expires them all. See src/game/buffs.hpp.
                party_buffs.clear();
                for (auto& who : party) {
                    who.buffs.clear();
                }
                // And the wearing-down starts again from nothing.
                hours_awake = 0;
                fatigue_hour = clock.minutes() / game::kMinutesPerHour;
                speak(party[0], 22);  // line 22: the waking word
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
                    // Only the destination changes here; the frame loop
                    // slides the geometry there at the door's own speed.
                    door.open = state == 2 ? !door.open : state != 0;
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
                // No script opcode names event sounds — a sweep of every
                // unnamed opcode's arguments against the sound table found
                // none — so the working of a door is this engine's choice
                // from the archive's own names. `inferred` The geometry
                // itself follows over the next frames, at the door's speed.
                ambient.play_once("stone door0101");
            } else if (!outcome.retextures.empty()) {
                ambient.play_once("WoodDRClose");
            }

            // A door into an establishment opens its counter.
            if (outcome.building != 0) {
                // Award 35, Freed Archibald, is granted by no script — the
                // measured negative — and everything about it converges on
                // the King's Library, row 168: its three 2DEvents rows are
                // the statue, freed and gone screens, and the two stray NPC
                // plates are its extra rows' ids. The executable does the
                // freeing; this engine reads walking in as the deed, and
                // marks the trigger's true precondition `unknown`.
                if (outcome.building == 168 && !script_state.awards.contains(35)) {
                    script_state.awards.insert(35);
                    pick_up_message = "The statue stirs: Archibald Ironfist is freed";
                    pick_up_shown = SDL_GetTicks();
                }
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
            // any trap its own record armed has its say. The check is the
            // original's, traced: the chest's trapped bit against the
            // acting character's Disarm and the map's lock number
            // (docs/formats/event-tables.md, "The chest flags word").
            if (outcome.chest >= 0 && !opened_chests.contains(outcome.chest)) {
                opened_chests.insert(outcome.chest);
                std::string took;
                const auto chest_index = static_cast<std::size_t>(outcome.chest);
                const bool trapped = chest_index < session.chest_flags.size() &&
                                     (session.chest_flags[chest_index] & 1) != 0;
                if (trapped) {
                    // The hirelings' promised points, one per trade held.
                    std::set<int> hireling_trades;
                    for (const auto& h : hirelings) {
                        if (h.benefit.disarm_bonus > 0) {
                            hireling_trades.insert(h.benefit.disarm_bonus);
                        }
                    }
                    int hireling_points = 0;
                    for (const int points : hireling_trades) {
                        hireling_points += points;
                    }
                    // The original spends the acting character's skill; this
                    // shell has no chosen portrait yet, so the best hand in
                    // the party stands in. `inferred`
                    int disarm = 0;
                    for (const auto& member : party) {
                        disarm = std::max(
                            disarm, game::character_disarm_value(member, hireling_points));
                    }
                    if (game::disarm_check(disarm, session.lock_difficulty, misc_random)) {
                        took = "A trap clicks, disarmed.  ";
                    } else {
                        const game::TrapElement element = game::trap_element(misc_random);
                        // One roll shared by the whole party, the Trap
                        // column's worth of d20s; each member may leap
                        // clear by Perception, and resistance answers the
                        // element for the rest.
                        const int rolled =
                            game::trap_damage(session.trap_difficulty, misc_random);
                        for (auto& member : party) {
                            int perception = 0;
                            if (const auto it = member.skills.find("Perception");
                                it != member.skills.end()) {
                                perception = it->second;
                            }
                            // The record already holds the packed byte the
                            // dodge wants; nothing needs repacking.
                            if (game::perception_dodges(perception, misc_random)) {
                                speak(member, 33);  // line 33: the close call's word
                                continue;
                            }
                            const int through = game::after_resistance(
                                rolled,
                                game::resistance_to(member, game::trap_element_type(element)));
                            member.hit_points = std::max(0, member.hit_points - through);
                        }
                        took = "A ";
                        took += game::trap_element_name(element);
                        took += " trap explodes!  ";
                    }
                }
                // The chest's own slots: the designers' fixed items and the
                // −1..−6 placeholders the generator resolves against the
                // map's treasure class.
                static const std::vector<world::MapItemInstance> kNoSlots;
                const auto& slots = chest_index < session.chest_items.size()
                                        ? session.chest_items[chest_index]
                                        : kNoSlots;
                for (const auto& rolled : game::chest_contents(
                         slots, static_cast<std::size_t>(session.treasure_level), random_items,
                         item_stats, standard_bonuses, special_bonuses,
                         static_cast<std::uint32_t>(outcome.chest + 1) * 40503U)) {
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
                // The chest's own face: its record's first word is the
                // DCHEST row, and that table's art runs CHEST01..CHEST08.
                const auto look =
                    static_cast<std::size_t>(outcome.chest) < session.chest_looks.size()
                        ? session.chest_looks[static_cast<std::size_t>(outcome.chest)]
                        : 0;
                chest_art = look < 8 ? static_cast<int>(look) : 0;
                chest_note = said_text;
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
                    // Bless and Heroism, out of the character's own slots:
                    // "a bonus to hit increased by 5 + 1 per point of skill"
                    // and "increases the damage ... by 5 + 1 point per
                    // point", which is what their rows say and what the
                    // slots now carry.
                    game::SkillPower swung = weapon_skill_of(party[who]);
                    if (const int blessed = party[who].buffs.power(game::CharacterBuff::Bless,
                                                                   clock.minutes());
                        blessed > 0) {
                        swung.to_hit += game::kBlessBase + blessed;
                    }
                    if (const int heroic = party[who].buffs.power(game::CharacterBuff::Heroism,
                                                                  clock.minutes());
                        heroic > 0) {
                        swung.damage += game::kBlessBase + heroic;
                    }
                    std::string blow =
                        battle.strike(target, party[who], packs[who], session, monster_stats,
                                      item_stats, random_items, standard_bonuses, special_bonuses,
                                      swung, rider.extra_damage, rider.damage_element);
                    if (!blow.empty()) {
                        if (blow.find(" and kills it") != std::string::npos) {
                            speak(party[who], 1);  // line 1: the victor's word
                        }
                        // The release and the blow: archive names picked by
                        // this engine, marked as such.
                        const int held_now =
                            party[who].equipped[static_cast<std::size_t>(game::Slot::Weapon)];
                        const auto* held_row =
                            held_now > 0 ? item_stats.at(static_cast<std::size_t>(held_now))
                                         : nullptr;
                        // The blow in the weapon's own voice: the archive
                        // names hits by kind and weight — sword, axe, blunt,
                        // arrow, light to heavy — and the skill group picks
                        // among them. The join is the engine's. `inferred`
                        if (held_row != nullptr &&
                            held_row->equip_type == data::ItemEquipType::Missile) {
                            ambient.play_once("ArchShoot");
                        } else {
                            const std::string kind =
                                held_row != nullptr ? held_row->skill_group : std::string();
                            const char weight = "lmh"[(SDL_GetTicks() / 97) % 3];
                            const char digit = static_cast<char>(
                                '1' + static_cast<int>((SDL_GetTicks() / 97) % 3));
                            std::string name;
                            if (kind == "Axe") {
                                name = std::string("hit with axe0") + digit + weight;
                            } else if (kind == "Mace" || kind == "Staff") {
                                name = std::string("hit with blunt weapon 0") + digit + weight;
                            } else {
                                name = std::string("hit with sword 0") + digit + weight;
                            }
                            ambient.play_once(name);
                        }
                        pick_up_message = std::move(blow);
                        pick_up_shown = SDL_GetTicks();
                        pending_round = turn_based;
                        // The original's own recovery, in `Rec` points: the
                        // table at 0x4c2750 answers for whatever is in hand
                        // — 100 for a bare fist — and worn armour and a
                        // held shield add their own entries on top, halved
                        // by their skill's expert line and gone at master.
                        // See src/game/skills.hpp.
                        const auto gear_of = [&](game::Slot slot) -> const data::ItemStatsEntry* {
                            const auto i = static_cast<std::size_t>(slot);
                            const int id = party[who].equipped[i];
                            if (id <= 0 || party[who].equipped_broken[i]) {
                                return nullptr;
                            }
                            return item_stats.at(static_cast<std::size_t>(id));
                        };
                        const auto lift_for = [&](const data::ItemStatsEntry& row) {
                            int points = 0;
                            if (const auto it = party[who].skills.find(row.skill_group);
                                it != party[who].skills.end()) {
                                points = game::skill_points(it->second);
                            }
                            const auto* skill = skill_table.find(row.skill_group);
                            return skill != nullptr && points > 0
                                       ? game::skill_power(skill->text, points).armor_penalty_lift
                                       : 0;
                        };
                        int rec_points = game::kBareHandRecovery;
                        if (const auto* held_row = gear_of(game::Slot::Weapon);
                            held_row != nullptr && !held_row->skill_group.empty()) {
                            // A group the table does not name — "Club", which
                            // ITEMS.TXT gives three weapons — leaves the
                            // bare-hand default standing, as the routine does.
                            if (const int cost = game::gear_recovery(held_row->skill_group);
                                cost > 0) {
                                rec_points = cost;
                            }
                        }
                        for (const game::Slot slot : {game::Slot::Armor, game::Slot::Shield}) {
                            const auto* row = gear_of(slot);
                            if (row == nullptr || row->skill_group.empty()) {
                                continue;
                            }
                            rec_points += game::worn_recovery_penalty(
                                game::gear_recovery(row->skill_group), lift_for(*row));
                        }
                        // The original drains recovery half again as fast for
                        // a character wearing an unbroken item "of Recovery",
                        // the special table's row 17 — traced at 0x488605.
                        // Haste keeps the same figure as this engine's own
                        // reading of the spell.
                        float haste = 1.0f;
                        for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                            if (party[who].equipped[slot] > 0 &&
                                !party[who].equipped_broken[slot] &&
                                party[who].worn_special[slot] == game::kSpecialOfRecovery) {
                                haste = game::kOfRecoveryDrain;
                                break;
                            }
                        }
                        if (clock.minutes() < party[who].haste_until) {
                            haste = game::kOfRecoveryDrain;
                        }
                        // What the routine then takes back off the total:
                        // the Speed bonus off the sheet's own ladder, the
                        // level of a Sword, Axe or Bow held at expert or
                        // better, and a flat 20 for anything worn "of
                        // Swiftness". The result floors at nothing.
                        // The routine scales the Speed term by the worst
                        // condition before the ladder reads it, so a poisoned
                        // character recovers as though slower.
                        rec_points -= game::attribute_bonus(game::ailing_attribute(
                            party[who], game::Attribute::Speed, game::kAgeRecoveryPercent));
                        if (const auto* held_row = gear_of(game::Slot::Weapon);
                            held_row != nullptr &&
                            game::skill_quickens_attack(held_row->skill_group)) {
                            if (const auto it = party[who].skills.find(held_row->skill_group);
                                it != party[who].skills.end() &&
                                game::rank_of(it->second) >= 1) {
                                rec_points -= game::skill_points(it->second);
                            }
                        }
                        for (std::size_t slot = 0; slot < game::kSlotCount; ++slot) {
                            const int id = party[who].equipped[slot];
                            if (id <= 0 || party[who].equipped_broken[slot]) {
                                continue;
                            }
                            const bool swift =
                                party[who].worn_special[slot] == game::kSpecialOfSwiftness ||
                                std::find(game::kSwiftArtifacts.begin(),
                                          game::kSwiftArtifacts.end(),
                                          id) != game::kSwiftArtifacts.end();
                            if (swift) {
                                rec_points -= game::kSwiftItemRelief;
                                break;
                            }
                        }
                        rec_points = std::max(0, rec_points);
                        party_recovery = game::recovery_seconds(rec_points) / haste;
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
        // A new month wipes the kill book; the hall posts a fresh head.
        {
            static int last_month = -1;
            const int month_now = static_cast<int>(clock.day() / 28);
            if (month_now != last_month) {
                last_month = month_now;
                kills_this_month.clear();
            }
        }
        // The sea shimmers: the water tiles' own blue rings rotated a
        // step every few ticks and re-baked. The cadence is the engine's.
        if (session.outdoor() && !session.water_tiles.empty()) {
            const int phase = static_cast<int>(SDL_GetTicks() / 180);
            if (phase != water_phase) {
                water_phase = phase;
                for (const auto& tile : session.water_tiles) {
                    image::Bitmap rebaked;
                    if (image::decode_bitmap_cycled(tile.raw, tile.ramp_lo, tile.ramp_len,
                                                    phase % tile.ramp_len, rebaked) ==
                        image::BitmapError::None) {
                        render::Texture texture;
                        if (render::Texture::create(rebaked.width, rebaked.height,
                                                    std::move(rebaked.rgba), texture)) {
                            (void)session.tiles.set(tile.index, std::move(texture));
                        }
                    }
                }
            }
        }
        // Step the moving wall textures.
        for (std::size_t g = 0; g < texture_loops.size(); ++g) {
            const auto& loop = texture_loops[g];
            if (loop.frames.size() < 2) {
                continue;
            }
            const std::string* frame = loop.frame_at(game::sprite_ticks(SDL_GetTicks()));
            if (frame != nullptr && *frame != texture_loop_shown[g]) {
                texture_loop_shown[g] = *frame;
                cache.alias_bitmap(loop.frames.front().name, *frame);
            }
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
                // Crossing below half is worth a word; line 34 is the
                // engine's pick for the pained one.
                if (party[i].hit_points * 2 < party[i].max_hit_points &&
                    known_hp[i] * 2 >= party[i].max_hit_points) {
                    speak(party[i], 34);
                }
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
            // The aimed monster's blood, in the game's own bar: MHP_BG
            // with its end caps across the viewport's top, the fill strip
            // green, yellow or red as the target falls. The thresholds
            // between the three strips are the engine's. `inferred`
            if (const std::size_t aimed_now =
                    game::aimed_actor(session, battle, camera.position, camera.forward(),
                                      game::kMissileRange);
                aimed_now != game::kNoActor) {
                const auto [hp, max_hp] = battle.health_of(aimed_now);
                if (max_hp > 0) {
                    const int left = 8 + (460 - 200) / 2;
                    const int top = 12;
                    blit(scene.framebuffer(), cache.icon("MHP_BG"), left, top);
                    const float frac =
                        std::clamp(static_cast<float>(hp) / static_cast<float>(max_hp), 0.0f,
                                   1.0f);
                    const char* strip = frac > 0.5f   ? "MHP_GRN"
                                        : frac > 0.25f ? "MHP_YEL"
                                                       : "MHP_RED";
                    const auto& fill = cache.icon(strip);
                    const int width = static_cast<int>(frac * 200.0f);
                    // The strip clipped to the blood left: rows copied by hand
                    // because blit has no width limit.
                    if (!fill.empty() && width > 0) {
                        auto fb = scene.framebuffer().color();
                        const auto src = fill.pixels();
                        for (int y = 0; y < static_cast<int>(fill.height()); ++y) {
                            for (int x = 0; x < width && x < static_cast<int>(fill.width());
                                 ++x) {
                                const auto si = (static_cast<std::size_t>(y) * fill.width() +
                                                 static_cast<std::size_t>(x)) * 4;
                                if (src[si + 3] == 0) {
                                    continue;
                                }
                                const auto di =
                                    (static_cast<std::size_t>(top + 2 + y) * kWidth +
                                     static_cast<std::size_t>(left + x)) * 4;
                                fb[di] = src[si];
                                fb[di + 1] = src[si + 1];
                                fb[di + 2] = src[si + 2];
                            }
                        }
                    }
                    blit(scene.framebuffer(), cache.icon("MHP_CAPL"), left - 5, top);
                    blit(scene.framebuffer(), cache.icon("MHP_CAPR"), left + 200, top);
                }
            }
            // The obelisk puzzle solves itself when the fifteenth
            // fragment lands: the events give autonotes 79..93, one per
            // outdoor map, and the completed set earns award 62 — the
            // grant's placement is the engine's, like the library's.
            if (!script_state.awards.contains(62)) {
                bool all15 = true;
                for (int note = 79; note <= 93 && all15; ++note) {
                    all15 = script_state.autonotes.contains(note);
                }
                if (all15) {
                    script_state.awards.insert(62);
                    pick_up_message = "The fifteen fragments align: the obelisk puzzle is solved";
                    pick_up_shown = SDL_GetTicks();
                }
            }
            // The arena's judge: the sand cleared with a challenge open
            // pays the purse and ticks the rank's own counter award. The
            // purse is the engine's number.
            if (arena_rank >= 0 && session.file_name == "zarena.blv" &&
                !battle.anything_alive()) {
                const int purse = 250 << arena_rank;
                gold += purse;
                script_state.awards.insert(84 + arena_rank);
                script_state.variables[240 + arena_rank] += 1;
                pick_up_message = "Victory!  The purse is " + std::to_string(purse) +
                                  " gold; the heralds record it";
                pick_up_shown = SDL_GetTicks();
                arena_rank = -1;
            }
            if (arena_rank < 0 && session.file_name == "zarena.blv" &&
                !battle.anything_alive()) {
                game::draw_text(scene.framebuffer(), font, 24, 40,
                                "The Arena: 1 Page, 2 Squire, 3 Knight, 4 Lord",
                                render::Color{235, 225, 170, 255},
                                render::Color{0, 0, 0, 255});
            }
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
                std::to_string(gold) + " gold, " + std::to_string(party_food) + " food";
            game::draw_text(scene.framebuffer(), font, 474, corner_y, purse,
                            render::Color{180, 175, 155, 255}, render::Color{0, 0, 0, 255});
            corner_y += corner_line;
            for (const auto& h : hirelings) {
                game::draw_text(scene.framebuffer(), font, 474, corner_y,
                                "+ " + h.name + ", " + h.profession,
                                render::Color{190, 190, 215, 255}, render::Color{0, 0, 0, 255});
                corner_y += corner_line;
            }
            if (turn_based) {
                // The game's own hourglass says the world is waiting: its 80
                // frames step forward as rounds resolve. The frames per round
                // are the engine's own pace.
                char sand[9];
                std::snprintf(sand, sizeof(sand), "HGLAS%03d", hourglass_turn * 8 % 80);
                blit(scene.framebuffer(), cache.icon(sand), 552, 339);
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
        if (book_member >= 0) {
            const auto& reader = party[static_cast<std::size_t>(book_member)];
            const auto schools = schools_known(reader);
            int points = 0;
            if (!schools.empty()) {
                const auto school = schools[static_cast<std::size_t>(
                    std::clamp(book_school, 0, static_cast<int>(schools.size()) - 1))];
                for (const int id : reader.known_spells) {
                    const auto* spell = spell_stats.at(static_cast<std::size_t>(id));
                    if (spell != nullptr && spell->school == school) {
                        points = spell_skill_of(reader, *spell);
                        break;
                    }
                }
            }
            draw_book(scene, font, cache, reader, spell_stats, book_school, book_pick,
                      readied[static_cast<std::size_t>(book_member)], points);
        }
        if (show_map) {
            const bool eye_now =
                clock.minutes() < eye_until ||
                std::any_of(hirelings.begin(), hirelings.end(),
                            [](const auto& h) { return h.benefit.wizard_eye; });
            draw_map_page(scene, session, camera.position, camera.forward(), map_tile_colors,
                          map_colors_ready, eye_now ? std::max(eye_rank, 1) : 0, battle);
        }
        if (show_quickref) {
            draw_quick_reference(scene, font, cache, party, stat_descriptions, gold, party_food);
        }
        if (show_options) {
            draw_options(scene, font, cache, window_scale, fullscreen, turn_based,
                         ini_always_run, ini_loud_music);
        }
        if (rest_screen && font.glyph_count() > 0) {
            // The camp: restmain's own panel, its three button slots worn
            // by restb1..restb3 and the exit plate, the food by the apple
            // and the hour on the green slab.
            blit(scene.framebuffer(), cache.icon("restmain"), 8, 8);
            blit(scene.framebuffer(), cache.icon("restb1"), 65, 241);
            blit(scene.framebuffer(), cache.icon("restb2"), 65, 271);
            blit(scene.framebuffer(), cache.icon("restb3"), 65, 301);
            blit(scene.framebuffer(), cache.icon("restexit"), 285, 308);
            game::draw_text(scene.framebuffer(), font, 34, 172,
                            std::to_string(party_food) + " food",
                            render::Color{50, 35, 20, 255}, {0, 0, 0, 0});
            game::draw_text(scene.framebuffer(), font, 280, 170, clock.hhmm(),
                            render::Color{215, 210, 190, 255}, render::Color{0, 0, 0, 255});
            game::draw_text(scene.framebuffer(), font, 280, 184,
                            "day " + std::to_string(clock.day() + 1) + ", " +
                                std::string(clock.weekday()),
                            render::Color{190, 185, 170, 255}, render::Color{0, 0, 0, 255});
            // The plates ship blank; the game writes their words, so we do.
            const render::Color plate_word{55, 38, 22, 255};
            game::draw_text(scene.framebuffer(), font, 78, 249, "1 Rest and Heal 8 Hours",
                            plate_word, {0, 0, 0, 0});
            game::draw_text(scene.framebuffer(), font, 78, 279, "2 Rest until Dawn", plate_word,
                            {0, 0, 0, 0});
            game::draw_text(scene.framebuffer(), font, 78, 309, "3 Wait 1 Hour", plate_word,
                            {0, 0, 0, 0});
            game::draw_text(scene.framebuffer(), font, 322, 318, "Get Up (Esc)", plate_word,
                            {0, 0, 0, 0});
            game::draw_text(scene.framebuffer(), font, 52, 210, "the fire is lit",
                            render::Color{200, 195, 180, 255}, render::Color{0, 0, 0, 255});
        }
        if (chest_art >= 0 && font.glyph_count() > 0) {
            blit(scene.framebuffer(), cache.icon("CHEST0" + std::to_string(chest_art + 1)), 8, 8);
            const int line = font.height() + 2;
            int y = 40;
            std::string word;
            int x = 40;
            for (std::size_t i = 0; i <= chest_note.size(); ++i) {
                const char ch = i < chest_note.size() ? chest_note[i] : ' ';
                if (ch != ' ') {
                    word += ch;
                    continue;
                }
                const int width = font.text_width(word + " ");
                if (x + width > 420) {
                    x = 40;
                    y += line;
                }
                game::draw_text(scene.framebuffer(), font, x, y, word,
                                render::Color{225, 215, 180, 255}, render::Color{0, 0, 0, 255});
                x += width;
                word.clear();
            }
            game::draw_text(scene.framebuffer(), font, 40, 320, "any key closes the lid",
                            render::Color{170, 165, 150, 255}, render::Color{0, 0, 0, 255});
        }
        if (show_calendar) {
            // The calendar page: TIME_BG's own 360x300, centred in the
            // viewport, with the clock's words on it.
            blit(scene.framebuffer(), cache.icon("TIME_BG"), 8 + (460 - 360) / 2,
                 8 + (344 - 300) / 2);
            const int line = font.height() + 2;
            int y = 8 + (344 - 300) / 2 + 90;
            for (const std::string text :
                 {"day " + std::to_string(clock.day() + 1) + ", " + std::string(clock.weekday()),
                  clock.hhmm()}) {
                game::draw_text(scene.framebuffer(), font,
                                kWidth / 2 - font.text_width(text) / 2 - 90 + 4, y, text,
                                render::Color{60, 42, 22, 255}, render::Color{0, 0, 0, 0});
                y += line;
            }
        }
        if (shown_member >= 0) {
            draw_sheet(scene, font, cache, party[static_cast<std::size_t>(shown_member)],
                       stat_descriptions, class_descriptions, clock.minutes(), award_texts,
                       script_state.awards, sheet_page, item_stats);
        }
        if (open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            advance_room(game::interior_video(
                face_of(*shops_here[static_cast<std::size_t>(open_shop)])));
        } else if (!room.video.empty()) {
            room = {};
            ambient.stop_room();
        }
        if (talking_to >= 0 && open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            const auto here = people_of(*shops_here[static_cast<std::size_t>(open_shop)]);
            if (talking_to < static_cast<int>(here.size())) {
                const auto person = patched(here[static_cast<std::size_t>(talking_to)]);
                draw_conversation(scene, font, cache, person.npc_id,
                                  game::talk_to(person, dialogue, personalities, trade_talk,
                                                clock, interface_words, party[0].name,
                                                game::face_is_female(party[0].face),
                                                standing_for(person.npc_id)),
                                  talk_answer,
                                  shops_here[static_cast<std::size_t>(open_shop)]);
            }
        } else if (open_shop >= 0 && open_shop < static_cast<int>(shops_here.size())) {
            data::BuildingStatsEntry shop = *shops_here[static_cast<std::size_t>(open_shop)];
            shop.picture = face_of(shop);
            if (const std::string inside = entrance_map_of(shop); !inside.empty()) {
                // The mouth of the dungeon, playing its own video, with one
                // choice to make.
                dress_service(scene, font, cache, shop);
                game::draw_text(scene.framebuffer(), font, 190, 300,
                                "Enter descends into " + data::cp1252_to_utf8(shop.name),
                                render::Color{235, 225, 170, 255}, render::Color{0, 0, 0, 255});
                game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                                "Enter descends, B turns away",
                                render::Color{170, 170, 170, 255}, render::Color{0, 0, 0, 255});
            } else if (shop.type == "Town Hall" || shop.type == "City Council") {
                dress_service(scene, font, cache, shop);
                const auto* head = bounty_of();
                const int line2 = font.height() + 2;
                int y = 44;
                if (head != nullptr) {
                    const int purse = head->level * 100;
                    game::draw_text(scene.framebuffer(), font, 190, y,
                                    "This month's bounty: " + data::cp1252_to_utf8(head->name) +
                                        ", " + std::to_string(purse) + " gold",
                                    render::Color{235, 225, 170, 255},
                                    render::Color{0, 0, 0, 255});
                    y += line2;
                    std::string status;
                    if (bounty_month_paid == bounty_month()) {
                        status = "The purse is already paid this month.";
                    } else if (kills_this_month.contains(head->id)) {
                        status = "The head is taken - Enter claims the purse.";
                    } else {
                        status = "The head is still out there.";
                    }
                    game::draw_text(scene.framebuffer(), font, 190, y, status,
                                    render::Color{200, 200, 200, 255},
                                    render::Color{0, 0, 0, 255});
                }
                game::draw_text(scene.framebuffer(), font, 12, kHeight - 17,
                                "Enter claims a taken bounty, T talk, B closes",
                                render::Color{170, 170, 170, 255}, render::Color{0, 0, 0, 255});
            } else if (game::is_temple(shop)) {
                draw_temple(scene, font, cache, shop, party, gold, shop_said);
            } else if (game::is_bank(shop)) {
                draw_bank(scene, font, cache, shop, gold, bank_gold, shop_said);
            } else if (game::is_training(shop)) {
                draw_training(scene, font, cache, shop, party, gold, shop_said);
            } else if (game::is_travel(shop)) {
                draw_travel(scene, font, cache, shop, game::routes_of(shop, map_stats),
                            game::fare_of(shop), clock, gold, shop_said);
            } else {
                draw_shop(scene, font, cache, shop, shop_stock, item_stats, merchant_words,
                          gold, shop_said, shop_pick);
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
            draw_conversation(scene, font, cache, 0,
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
                         script_state.awards, autonote_texts, script_state.autonotes,
                         journal_page);
        }
        if (creating) {
            draw_creation(scene, font, cache, party, create_slot, stat_descriptions,
                          class_descriptions);
        }

        if (!movie_queue.empty()) {
            const std::string& reel = movie_queue.front();
            if (movie.video != reel) {
                movie = {};
                movie.video = reel;
                movie.ready = cache.interior_bytes(reel, movie.bytes) &&
                              video::SmackerDecoder::load(movie.bytes, movie.decoder) ==
                                  video::SmackerError::None &&
                              movie.decoder.info().frame_count > 0;
                movie.frame = 0;
                movie.next_at = 0;
                if (!movie.ready) {
                    movie_queue.erase(movie_queue.begin());
                }
            }
            if (movie.ready && SDL_GetTicks() >= movie.next_at) {
                std::span<const std::uint8_t> rgba;
                if (movie.decoder.decode_frame_rgba(movie.frame, rgba) ==
                    video::SmackerError::None) {
                    std::vector<std::uint8_t> pixels(rgba.begin(), rgba.end());
                    (void)render::Texture::create(
                        static_cast<std::uint16_t>(movie.decoder.info().width),
                        static_cast<std::uint16_t>(movie.decoder.info().height),
                        std::move(pixels), movie_frame);
                    video::SmackerAudioFrame chunk;
                    if (mouse_look &&
                        movie.decoder.decode_audio(movie.frame, 0, chunk) ==
                            video::SmackerError::None &&
                        !chunk.samples.empty()) {
                        const auto track = movie.decoder.audio_info(0);
                        ambient.play_room_chunk(chunk.samples.data(), chunk.samples.size(),
                                                static_cast<int>(track.sample_rate),
                                                track.stereo);
                    }
                }
                ++movie.frame;
                if (movie.frame >= movie.decoder.info().frame_count) {
                    movie_queue.erase(movie_queue.begin());
                    movie = {};
                    ambient.stop_room();
                } else {
                    const double fps =
                        movie.decoder.info().fps > 1.0 ? movie.decoder.info().fps : 15.0;
                    movie.next_at =
                        SDL_GetTicks() + static_cast<std::uint64_t>(1000.0 / fps);
                }
            }
            if (!movie_frame.empty()) {
                // Centred on black, at its own size.
                auto pixels = scene.framebuffer().color();
                std::fill(pixels.begin(), pixels.end(), 0);
                blit(scene.framebuffer(), movie_frame,
                     (kWidth - static_cast<int>(movie_frame.width())) / 2,
                     (kHeight - static_cast<int>(movie_frame.height())) / 2);
            }
        } else if (at_title) {
            blit(scene.framebuffer(), cache.icon("MM6TITLE.PCX"), 0, 0);
            const std::array<const char*, 4> kPlates{"MMNEW1", "MMLOA1", "MMCRE1", "MMESC1"};
            for (int i = 0; i < 4; ++i) {
                blit(scene.framebuffer(), cache.icon(kPlates[static_cast<std::size_t>(i)]),
                     20 + i * 152, 424);
            }
            if (title_credits && font.glyph_count() > 0) {
                game::draw_text(scene.framebuffer(), font, 24, 24,
                                "StarHaven, an open engine for your own copy of the game.",
                                render::Color{235, 225, 180, 255}, render::Color{0, 0, 0, 255});
                game::draw_text(scene.framebuffer(), font, 24, 24 + font.height() + 2,
                                "The art, the words and the world belong to their rights holders.",
                                render::Color{235, 225, 180, 255}, render::Color{0, 0, 0, 255});
            }
        }
        // The map's name, drawn with the game's own font, inside the
        // viewport's frame rather than across it.
        if (font.glyph_count() > 0 && !creating && !show_journal && book_member < 0 &&
            !at_title) {
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
