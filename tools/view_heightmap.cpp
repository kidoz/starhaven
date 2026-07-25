#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/lod/game_lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/odm_map.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <map.odm> [--scale N]\n"
              << "\n"
              << "Decompresses one .odm outdoor map from your own legal game\n"
              << "install (Games.lod) and renders its heightmap as a grayscale\n"
              << "image in an SDL3 window. Higher elevations are brighter.\n"
              << "Press ESC or close the window to quit.\n"
              << "\n"
              << "Set " << openmm6::platform::kInstallEnvVar
              << " to the install directory.\n";
}

std::filesystem::path resolve_games_lod() {
    namespace fs = std::filesystem;
    if (auto install = openmm6::platform::install_from_env()) {
        fs::path p = *install / "data" / "Games.lod";
        if (fs::exists(p)) {
            return p;
        }
    }
    return "data/Games.lod";
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_name;
    int scale = 4;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--scale") {
            if (++i >= argc) {
                print_usage(argv[0]);
                return 2;
            }
            scale = std::max(1, std::atoi(argv[i]));
        } else if (map_name.empty()) {
            map_name = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (map_name.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    namespace lod = openmm6::lod;
    namespace world = openmm6::world;

    lod::GameLodArchive archive;
    const lod::GameLodError open_err =
        lod::GameLodArchive::open(resolve_games_lod(), archive);
    if (open_err != lod::GameLodError::None) {
        std::cerr << "error: could not open Games.lod ("
                  << static_cast<int>(open_err) << ")\n";
        return 1;
    }
    std::span<const std::byte> entry;
    if (archive.payload(map_name, entry) != lod::GameLodArchive::PayloadError::None) {
        std::cerr << "error: map not found: " << map_name << "\n";
        return 1;
    }

    world::OdmMap map;
    world::OdmTerrain terrain;
    if (world::parse_odm_terrain(entry, map, terrain) != world::OdmError::None) {
        std::cerr << "error: could not parse ODM\n";
        return 1;
    }

    // Build a grayscale RGBA image from the heightmap. Brighter = higher.
    const auto [hmin, hmax] = std::minmax_element(terrain.heightmap.begin(),
                                                  terrain.heightmap.end());
    const int lo = *hmin;
    const int span = std::max(1, *hmax - lo);
    const int dim = world::OdmTerrain::kGridDim;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(dim) * dim * 4, 0);
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            const int h = terrain.heightmap[y * dim + x];
            const int g = ((h - lo) * 255) / span;
            std::uint8_t* px = &rgba[(y * dim + x) * 4];
            px[0] = static_cast<std::uint8_t>(g);
            px[1] = static_cast<std::uint8_t>(g);
            px[2] = static_cast<std::uint8_t>(g);
            px[3] = 255;
        }
    }
    std::cout << "rendering heightmap for " << map_name << " (" << dim << "x"
              << dim << ", height range " << lo << ".." << *hmax << ")\n";

    // SDL3 returns true on success, unlike SDL2's 0-on-success convention.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "error: SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string title = "openmm6 — heightmap — " + map_name;
    // SDL3 drops the x/y arguments and SDL_WINDOW_SHOWN; nullptr picks the
    // default render backend.
    SDL_Window* window =
        SDL_CreateWindow(title.c_str(), dim * scale, dim * scale, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, dim, dim);
    SDL_UpdateTexture(texture, nullptr, rgba.data(), dim * 4);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        // SDL3 renders at subpixel precision, so rects are float-valued.
        SDL_FRect dst{0.0f, 0.0f, static_cast<float>(dim * scale),
                      static_cast<float>(dim * scale)};
        SDL_RenderTexture(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
