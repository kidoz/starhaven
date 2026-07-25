#ifndef STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP
#define STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "core/image/palette.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/render/texture.hpp"

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

    // A sprite from SPRITES.LOD, decoded through the shared palette its header
    // names. Returns an empty texture when the name does not resolve.
    [[nodiscard]] const render::Texture& sprite(const std::string& name);

    // The same, but recoloured through a palette the caller names instead of
    // the sprite's own. The sprite frame table uses this to give a monster's
    // B and C variants their colours: all three share one picture and differ
    // only in palette. Pass kSpritePaletteFromHeader for the default.
    [[nodiscard]] const render::Texture& sprite(const std::string& name, int palette_override);

    // Whether SPRITES.LOD holds an entry, without decoding it. Used to probe
    // candidate names before committing to one.
    [[nodiscard]] bool has_sprite(const std::string& name);

    [[nodiscard]] std::size_t bitmap_count() const noexcept;
    [[nodiscard]] std::size_t sprite_count() const noexcept;

private:
    lod::LodArchive bitmap_archive_;
    lod::LodArchive sprite_archive_;
    bool bitmaps_open_ = false;
    bool sprites_open_ = false;

    // Cached decodes, including failures: an empty texture means "looked up and
    // not found", so a missing name costs one archive probe rather than one
    // per frame.
    std::map<std::string, render::Texture> bitmaps_;
    std::map<std::string, render::Texture> sprites_;
    std::map<std::uint16_t, image::Palette> palettes_;
};

}  // namespace starhaven::assets

#endif  // STARHAVEN_CORE_ASSETS_ASSET_CACHE_HPP
