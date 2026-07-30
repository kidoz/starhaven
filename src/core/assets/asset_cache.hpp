#ifndef STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP
#define STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/image/palette.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/render/texture.hpp"
#include "core/video/vid_archive.hpp"

namespace starhaven::assets {

// Loads and caches the textures a scene needs, resolving each name through the
// game's archives.
//
// Both walkers used to carry their own copy of this: open the archive, decode
// the entry, build a texture, remember it. Doing it once means a name that
// fails to resolve fails the same way everywhere.
// Palette override meaning "use the sprite's own".
inline constexpr int kSpritePaletteFromHeader = -1;

class AssetCache {
public:
    AssetCache() = default;

    // Open the archives under an installation's `data` directory. A missing
    // archive is not fatal: lookups against it fail, so a partial installation
    // still renders what it can.
    void open(const std::filesystem::path& data_dir);

    // A bitmap from BITMAPS.LOD (wall, ground and model textures). Returns an
    // empty texture when the name does not resolve.
    [[nodiscard]] const render::Texture& bitmap(const std::string& name);

    // A bitmap from icons.lod: interface art, and the twelve character
    // portraits. Same format as BITMAPS.LOD's, a different archive.
    [[nodiscard]] const render::Texture& icon(const std::string& name);

    // A sprite from SPRITES.LOD, decoded through the shared palette its header
    // names. Returns an empty texture when the name does not resolve.
    [[nodiscard]] const render::Texture& sprite(const std::string& name);

    // The same, but recoloured through a palette the caller names instead of
    // the sprite's own. The sprite frame table uses this to give a monster's
    // B and C variants their colours: all three share one picture and differ
    // only in palette. Pass kSpritePaletteFromHeader for the default.
    [[nodiscard]] const render::Texture& sprite(const std::string& name, int palette_override);

    // The first frame of a named interior video from the install's
    // Anims*.vid archives, as a still. Returns an empty texture when the
    // archives or the name are absent — a partial install shows marble.
    [[nodiscard]] const render::Texture& interior(const std::string& name);

    // The same entry's raw bytes, for a caller that plays the video live
    // rather than hanging its first frame on the wall.
    [[nodiscard]] bool interior_bytes(const std::string& name, std::vector<std::byte>& out);

    // Replace the cached still with a live frame: the interior player
    // steps the video and every screen that asks for the room sees the
    // current frame through the same lookup.
    void set_interior(const std::string& name, render::Texture texture) {
        interiors_[name] = std::move(texture);
    }

    // Show one bitmap under another's name: the texture frame table's
    // animation, done by replacing the cached still each tick.
    void alias_bitmap(const std::string& shown_as, const std::string& source) {
        bitmaps_[shown_as] = bitmap(source);
    }

    // Whether SPRITES.LOD holds an entry, without decoding it. Used to probe
    // candidate names before committing to one.
    [[nodiscard]] bool has_sprite(const std::string& name);

    [[nodiscard]] std::size_t bitmap_count() const noexcept;
    [[nodiscard]] std::size_t sprite_count() const noexcept;

private:
    lod::LodArchive bitmap_archive_;
    lod::LodArchive icon_archive_;
    lod::LodArchive sprite_archive_;
    bool bitmaps_open_ = false;
    bool icons_open_ = false;
    bool sprites_open_ = false;

    // Cached decodes, including failures: an empty texture means "looked up and
    // not found", so a missing name costs one archive probe rather than one
    // per frame.
    std::filesystem::path install_root_;  // where the Anims archives live
    bool anims_open_ = false;
    bool anims_tried_ = false;
    std::array<video::VidArchive, 2> anims_;

    std::map<std::string, render::Texture> bitmaps_;
    std::map<std::string, render::Texture> interiors_;
    std::map<std::string, render::Texture> icons_;
    std::map<std::string, render::Texture> sprites_;
    std::map<std::uint16_t, image::Palette> palettes_;
};

}  // namespace starhaven::assets

#endif  // STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP
