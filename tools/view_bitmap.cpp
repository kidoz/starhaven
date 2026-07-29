#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/image/bitmap.hpp"
#include "core/image/palette.hpp"
#include "core/image/sprite.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <archive.lod> <entry> [--scale N]\n"
              << "\n"
              << "Decodes one .LOD image entry from your own legal game install\n"
              << "and displays it in an SDL3 window. Press ESC or close the window to quit.\n"
              << "\n"
              << "  --scale N   integer nearest-neighbor upscale factor (default 1)\n"
              << "\n"
              << "Set " << starhaven::platform::kInstallEnvVar
              << " to the install directory, or pass the full archive path.\n";
}

std::filesystem::path resolve_archive(const std::string& arg) {
    namespace fs = std::filesystem;
    fs::path p(arg);
    if (p.is_absolute() && fs::exists(p)) {
        return p;
    }
    if (auto install = starhaven::platform::install_from_env()) {
        for (fs::path candidate : {*install / arg, *install / "data" / arg}) {
            if (fs::exists(candidate)) {
                return candidate;
            }
        }
    }
    return p;
}

// A decoded image in a renderer-agnostic form.
struct DecodedImage {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> rgba;  // width*height*4
    bool is_sprite = false;
};

// Load a `palXXX` palette entry from BITMAPS.LOD given a numeric palette id.
// Sprites reference their palette this way. Returns false if the palette cannot
// be found or extracted.
bool load_palette(std::uint16_t palette_id, starhaven::image::Palette& out) {
    namespace fs = std::filesystem;
    namespace lod = starhaven::lod;
    namespace img = starhaven::image;

    const std::string pal_name = img::palette_entry_name(palette_id);
    fs::path bitmaps = resolve_archive("data/BITMAPS.LOD");
    if (!fs::exists(bitmaps)) {
        std::cerr << "error: cannot locate data/BITMAPS.LOD for palette " << pal_name << "\n";
        return false;
    }

    lod::LodArchive archive;
    if (lod::LodArchive::open(bitmaps, archive) != lod::LodError::None) {
        return false;
    }
    std::span<const std::byte> bytes;
    if (archive.payload(pal_name, bytes) != lod::LodArchive::PayloadError::None) {
        std::cerr << "error: palette entry not found: " << pal_name << "\n";
        return false;
    }
    // Palette entries are 48-byte zero-image headers followed by 768 RGB bytes.
    return img::extract_palette(bytes, /*data_offset=*/48, out) == img::PaletteError::None;
}

}  // namespace

int main(int argc, char** argv) {
    std::string archive_arg;
    std::string entry_name;
    int scale = 1;

    std::string dump_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dump") {
            if (++i >= argc) {
                print_usage(argv[0]);
                return 2;
            }
            dump_path = argv[i];
        } else if (a == "--scale") {
            if (++i >= argc) {
                print_usage(argv[0]);
                return 2;
            }
            scale = std::max(1, std::atoi(argv[i]));
        } else if (archive_arg.empty()) {
            archive_arg = a;
        } else if (entry_name.empty()) {
            entry_name = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (archive_arg.empty() || entry_name.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    namespace lod = starhaven::lod;
    namespace img = starhaven::image;

    lod::LodArchive archive;
    const lod::LodError open_err = lod::LodArchive::open(resolve_archive(archive_arg), archive);
    if (open_err != lod::LodError::None) {
        std::cerr << "error: could not open archive (" << static_cast<int>(open_err) << ")\n";
        return 1;
    }

    std::span<const std::byte> entry_bytes;
    const auto payload_err = archive.payload(entry_name, entry_bytes);
    if (payload_err == lod::LodArchive::PayloadError::NotFound) {
        std::cerr << "error: entry not found: " << entry_name << "\n";
        return 1;
    }
    if (payload_err == lod::LodArchive::PayloadError::Compressed) {
        std::cerr << "error: the LOD entry is compressed at the container level;\n"
                  << "       this viewer only handles image-level compression.\n";
        return 2;
    }
    if (payload_err != lod::LodArchive::PayloadError::None) {
        std::cerr << "error: could not read entry payload (" << static_cast<int>(payload_err)
                  << ")\n";
        return 1;
    }

    // Decode the entry. Try the sprite path first (sprites live in SPRITES.LOD
    // and reference a shared palette); fall back to the bitmap path (images in
    // BITMAPS.LOD / icons.lod that embed their own palette).
    DecodedImage image;
    const std::string archive_lower = [&] {
        std::string s = archive_arg;
        for (auto& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }();
    const bool likely_sprite = archive_lower.find("sprite") != std::string::npos;

    if (likely_sprite) {
        img::SpriteHeader sh;
        if (img::read_sprite_header(entry_bytes, sh) == img::SpriteError::None) {
            img::Palette palette;
            if (!load_palette(sh.palette_id, palette)) {
                return 1;
            }
            img::Sprite sprite;
            const img::SpriteError se = img::decode_sprite(entry_bytes, palette, sprite);
            if (se != img::SpriteError::None) {
                std::cerr << "error: could not decode sprite (" << static_cast<int>(se) << ")\n";
                return 1;
            }
            image.width = sprite.width;
            image.height = sprite.height;
            image.rgba = std::move(sprite.rgba);
            image.is_sprite = true;
        }
    }

    if (image.rgba.empty()) {
        img::Bitmap bitmap;
        const img::BitmapError de = img::decode_bitmap(entry_bytes, bitmap);
        if (de != img::BitmapError::None) {
            std::cerr << "error: could not decode as sprite or bitmap (bitmap err "
                      << static_cast<int>(de) << ")\n";
            return 1;
        }
        image.width = bitmap.width;
        image.height = bitmap.height;
        image.rgba = std::move(bitmap.rgba);
    }

    std::cout << "decoded " << entry_name << ": " << image.width << "x" << image.height
              << (image.is_sprite ? " (sprite)" : " (bitmap)") << " (showing at x" << scale
              << ")\n";

    // --dump writes the pixels and skips the window, for headless research.
    if (!dump_path.empty()) {
        std::ofstream out(dump_path, std::ios::binary);
        out << "P6\n" << image.width << " " << image.height << "\n255\n";
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            out.put(static_cast<char>(image.rgba[i]));
            out.put(static_cast<char>(image.rgba[i + 1]));
            out.put(static_cast<char>(image.rgba[i + 2]));
        }
        return out.good() ? 0 : 1;
    }

    // SDL3 returns true on success, unlike SDL2's 0-on-success convention.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    const int win_w = static_cast<int>(image.width) * scale;
    const int win_h = static_cast<int>(image.height) * scale;
    const std::string title = "StarHaven — " + entry_name;

    // SDL3 drops the x/y arguments (the window manager places the window) and
    // SDL_WINDOW_SHOWN, since windows are now shown unless SDL_WINDOW_HIDDEN.
    SDL_Window* window = SDL_CreateWindow(title.c_str(), win_w, win_h, 0);
    if (!window) {
        std::cerr << "error: SDL_CreateWindow: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    // SDL3 selects the backend by name; nullptr means "let SDL choose", which
    // already prefers an accelerated driver. VSync is a separate call now.
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "error: SDL_CreateRenderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    // The image is top-to-bottom RGBA; SDL expects the same row order. Nearest
    // scaling keeps the pixel-art look crisp when upscaling.
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (!texture) {
        std::cerr << "error: SDL_CreateTexture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_UpdateTexture(texture, nullptr, image.rgba.data(), static_cast<int>(image.width) * 4);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }
        // Checkerboard background so transparent sprite pixels are visible.
        SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
        SDL_RenderClear(renderer);
        // Destination rect honors the integer --scale factor; source is whole.
        // SDL3 renders at subpixel precision, so rects are float-valued.
        SDL_FRect dst{0.0f, 0.0f, static_cast<float>(image.width) * static_cast<float>(scale),
                      static_cast<float>(image.height) * static_cast<float>(scale)};
        SDL_RenderTexture(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
