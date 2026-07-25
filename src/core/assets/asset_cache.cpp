#include "core/assets/asset_cache.hpp"

#include "core/image/bitmap.hpp"
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
    bitmaps_open_ = lod::LodArchive::open(data_dir / "BITMAPS.LOD",
                                          bitmap_archive_) == lod::LodError::None;
    sprites_open_ = lod::LodArchive::open(data_dir / "SPRITES.LOD",
                                          sprite_archive_) == lod::LodError::None;
}

std::size_t AssetCache::bitmap_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(bitmaps_.begin(), bitmaps_.end(),
                      [](const auto& kv) { return !kv.second.empty(); }));
}

std::size_t AssetCache::sprite_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(sprites_.begin(), sprites_.end(),
                      [](const auto& kv) { return !kv.second.empty(); }));
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
            (void)render::Texture::create(bmp.width, bmp.height,
                                          std::move(bmp.rgba), texture);
        }
    }
    // Cache failures too, so a name that never resolves is probed once.
    return bitmaps_.emplace(name, std::move(texture)).first->second;
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
    if (const auto it = sprites_.find(name); it != sprites_.end()) {
        return it->second;
    }
    render::Texture texture;
    std::span<const std::byte> raw;
    if (sprites_open_ && !name.empty() &&
        sprite_archive_.payload(name, raw) == lod::LodArchive::PayloadError::None) {
        image::SpriteHeader header;
        if (image::read_sprite_header(raw, header) == image::SpriteError::None) {
            // Sprites share palettes held in BITMAPS.LOD; decode each once.
            image::Palette palette;
            bool have_palette = false;
            if (const auto cached = palettes_.find(header.palette_id);
                cached != palettes_.end()) {
                palette = cached->second;
                have_palette = true;
            } else if (bitmaps_open_) {
                std::span<const std::byte> pal_bytes;
                if (bitmap_archive_.payload(
                        image::palette_entry_name(header.palette_id), pal_bytes) ==
                        lod::LodArchive::PayloadError::None &&
                    // Palette entries are a 48-byte zero-image header followed
                    // by 768 RGB bytes.
                    image::extract_palette(pal_bytes, /*data_offset=*/48, palette) ==
                        image::PaletteError::None) {
                    palettes_.emplace(header.palette_id, palette);
                    have_palette = true;
                }
            }
            if (have_palette) {
                image::Sprite decoded;
                if (image::decode_sprite(raw, palette, decoded) ==
                    image::SpriteError::None) {
                    (void)render::Texture::create(decoded.width, decoded.height,
                                                  std::move(decoded.rgba), texture);
                }
            }
        }
    }
    return sprites_.emplace(name, std::move(texture)).first->second;
}

}  // namespace starhaven::assets
