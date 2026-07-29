#include "core/assets/asset_cache.hpp"

#include "core/image/bitmap.hpp"
#include "core/image/pcx.hpp"
#include "core/image/sprite.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace starhaven::assets {

namespace {
// Returned for any name that does not resolve.
const render::Texture kEmpty{};
}  // namespace

void AssetCache::open(const std::filesystem::path& data_dir) {
    bitmaps_open_ =
        lod::LodArchive::open(data_dir / "BITMAPS.LOD", bitmap_archive_) == lod::LodError::None;
    sprites_open_ =
        lod::LodArchive::open(data_dir / "SPRITES.LOD", sprite_archive_) == lod::LodError::None;
    icons_open_ =
        lod::LodArchive::open(data_dir / "icons.lod", icon_archive_) == lod::LodError::None;
}

std::size_t AssetCache::bitmap_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        bitmaps_.begin(), bitmaps_.end(), [](const auto& kv) { return !kv.second.empty(); }));
}

std::size_t AssetCache::sprite_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        sprites_.begin(), sprites_.end(), [](const auto& kv) { return !kv.second.empty(); }));
}

const render::Texture& AssetCache::bitmap(const std::string& name) {
    if (const auto it = bitmaps_.find(name); it != bitmaps_.end()) {
        return it->second;
    }
    render::Texture texture;
    std::span<const std::byte> raw;
    if (bitmaps_open_ && !name.empty() &&
        bitmap_archive_.payload(name, raw) == lod::LodArchive::PayloadError::None) {
        image::Bitmap bmp;
        if (image::decode_bitmap(raw, bmp) == image::BitmapError::None) {
            (void)render::Texture::create(bmp.width, bmp.height, std::move(bmp.rgba), texture);
        }
    }
    // Cache failures too, so a name that never resolves is probed once.
    return bitmaps_.emplace(name, std::move(texture)).first->second;
}

const render::Texture& AssetCache::icon(const std::string& name) {
    if (const auto it = icons_.find(name); it != icons_.end()) {
        return it->second;
    }
    render::Texture texture;
    std::span<const std::byte> raw;
    if (icons_open_ && !name.empty() &&
        icon_archive_.payload(name, raw) == lod::LodArchive::PayloadError::None) {
        image::Bitmap bmp;
        // A few interface panels ship as PCX in the same container; the
        // bitmap reader rejects them by their zeroed dimensions.
        if (image::decode_bitmap(raw, bmp) == image::BitmapError::None ||
            image::decode_pcx_entry(raw, bmp) == image::BitmapError::None) {
            (void)render::Texture::create(bmp.width, bmp.height, std::move(bmp.rgba), texture);
        }
    }
    return icons_.emplace(name, std::move(texture)).first->second;
}

bool AssetCache::has_sprite(const std::string& name) {
    if (const auto it = sprites_.find(name); it != sprites_.end()) {
        return !it->second.empty();
    }
    if (!sprites_open_ || name.empty()) {
        return false;
    }
    std::span<const std::byte> raw;
    return sprite_archive_.payload(name, raw) == lod::LodArchive::PayloadError::None;
}

const render::Texture& AssetCache::sprite(const std::string& name) {
    return sprite(name, kSpritePaletteFromHeader);
}

const render::Texture& AssetCache::sprite(const std::string& name, int palette_override) {
    // One sprite drawn through two palettes is two textures, so the palette is
    // part of the key. Monster variants rely on this: ArcherA, ArcherB and
    // ArcherC are one picture and three palettes.
    const std::string key =
        palette_override < 0 ? name : name + "#" + std::to_string(palette_override);
    if (const auto it = sprites_.find(key); it != sprites_.end()) {
        return it->second;
    }
    render::Texture texture;
    std::span<const std::byte> raw;
    if (sprites_open_ && !name.empty() &&
        sprite_archive_.payload(name, raw) == lod::LodArchive::PayloadError::None) {
        image::SpriteHeader header;
        if (image::read_sprite_header(raw, header) == image::SpriteError::None) {
            const std::uint16_t palette_id = palette_override < 0
                                                 ? header.palette_id
                                                 : static_cast<std::uint16_t>(palette_override);
            // Sprites share palettes held in BITMAPS.LOD; decode each once.
            image::Palette palette;
            bool have_palette = false;
            if (const auto cached = palettes_.find(palette_id); cached != palettes_.end()) {
                palette = cached->second;
                have_palette = true;
            } else if (bitmaps_open_) {
                std::span<const std::byte> pal_bytes;
                if (bitmap_archive_.payload(image::palette_entry_name(palette_id), pal_bytes) ==
                        lod::LodArchive::PayloadError::None &&
                    // Palette entries are a 48-byte zero-image header followed
                    // by 768 RGB bytes.
                    image::extract_palette(pal_bytes, /*data_offset=*/48, palette) ==
                        image::PaletteError::None) {
                    palettes_.emplace(palette_id, palette);
                    have_palette = true;
                }
            }
            if (have_palette) {
                image::Sprite decoded;
                if (image::decode_sprite(raw, palette, decoded) == image::SpriteError::None) {
                    (void)render::Texture::create(decoded.width, decoded.height,
                                                  std::move(decoded.rgba), texture);
                }
            }
        }
    }
    return sprites_.emplace(key, std::move(texture)).first->second;
}

}  // namespace starhaven::assets
