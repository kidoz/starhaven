#include "game/install_prompt.hpp"

#include <mutex>
#include <optional>
#include <string>

#include <SDL3/SDL.h>

namespace starhaven::game {

namespace {

struct DialogResult {
    std::mutex mutex;
    bool complete = false;
    bool failed = false;
    std::optional<std::string> selected;
};

void SDLCALL folder_selected(void* userdata, const char* const* file_list, int) {
    auto& result = *static_cast<DialogResult*>(userdata);
    std::lock_guard lock(result.mutex);
    result.complete = true;
    result.failed = file_list == nullptr;
    if (file_list != nullptr && file_list[0] != nullptr) {
        result.selected = file_list[0];
    }
}

void draw_prompt(SDL_Renderer* renderer, const std::string& diagnostic, bool dialog_open) {
    SDL_SetRenderDrawColor(renderer, 14, 10, 24, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderScale(renderer, 2.0f, 2.0f);

    SDL_SetRenderDrawColor(renderer, 232, 203, 122, 255);
    SDL_RenderDebugText(renderer, 20.0f, 18.0f, "STARHAVEN");
    SDL_SetRenderDrawColor(renderer, 232, 232, 224, 255);
    SDL_RenderDebugText(renderer, 20.0f, 42.0f, "MIGHT AND MAGIC VI RESOURCES REQUIRED");
    SDL_RenderDebugText(renderer, 20.0f, 66.0f,
                        "StarHaven reads your legal installation in place.");
    SDL_RenderDebugText(renderer, 20.0f, 78.0f, "It will not copy or modify the game's files.");

    SDL_SetRenderDrawColor(renderer, 238, 150, 118, 255);
    SDL_RenderDebugText(renderer, 20.0f, 108.0f, diagnostic.c_str());
    SDL_SetRenderDrawColor(renderer, 202, 202, 198, 255);
    SDL_RenderDebugText(renderer, 20.0f, 126.0f,
                        "Choose the folder that contains the Data folder.");

    const SDL_FRect button{40.0f, 170.0f, 240.0f, 28.0f};
    SDL_SetRenderDrawColor(renderer, dialog_open ? 72 : 92, dialog_open ? 62 : 74,
                           dialog_open ? 78 : 104, 255);
    SDL_RenderFillRect(renderer, &button);
    SDL_SetRenderDrawColor(renderer, 244, 226, 170, 255);
    SDL_RenderRect(renderer, &button);
    SDL_RenderDebugText(renderer, dialog_open ? 92.0f : 75.0f, 180.0f,
                        dialog_open ? "FOLDER DIALOG OPEN" : "CHOOSE INSTALLATION FOLDER");
    SDL_RenderDebugText(renderer, 20.0f, 218.0f,
                        dialog_open ? "Complete or cancel the folder dialog."
                                    : "Enter/click to choose. Escape exits.");

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_RenderPresent(renderer);
}

}  // namespace

std::optional<platform::GameInstall>
prompt_for_game_install(const platform::InstallValidation& initial_validation) {
    const bool own_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (own_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return std::nullopt;
    }

    SDL_Window* window = SDL_CreateWindow("StarHaven - Choose MM6 Installation", 640, 480, 0);
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (window == nullptr || renderer == nullptr) {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        if (own_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return std::nullopt;
    }

    std::string diagnostic = platform::install_problem_message(initial_validation);
    DialogResult dialog;
    bool dialog_open = false;
    bool quit_requested = false;
    std::optional<platform::GameInstall> selected_install;

    while (!selected_install && !(quit_requested && !dialog_open)) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                quit_requested = true;
            }
            const bool choose =
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN) ||
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 event.button.button == SDL_BUTTON_LEFT && event.button.y >= 340.0f &&
                 event.button.y <= 396.0f);
            if (choose && !dialog_open) {
                {
                    std::lock_guard lock(dialog.mutex);
                    dialog.complete = false;
                    dialog.failed = false;
                    dialog.selected.reset();
                }
                SDL_ShowOpenFolderDialog(folder_selected, &dialog, window, nullptr, false);
                dialog_open = true;
            }
        }

        std::optional<std::string> chosen;
        bool dialog_failed = false;
        {
            std::lock_guard lock(dialog.mutex);
            if (dialog.complete) {
                dialog_open = false;
                chosen = dialog.selected;
                dialog_failed = dialog.failed;
                dialog.complete = false;
                dialog.selected.reset();
            }
        }
        if (dialog_failed) {
            diagnostic = "The system folder dialog could not be opened.";
        } else if (chosen) {
            platform::InstallValidation validation =
                platform::validate_game_install(std::filesystem::path{*chosen});
            diagnostic = platform::install_problem_message(validation);
            if (validation.valid()) {
                selected_install = std::move(validation.install);
            }
        }

        draw_prompt(renderer, diagnostic, dialog_open);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (own_video) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return selected_install;
}

}  // namespace starhaven::game
