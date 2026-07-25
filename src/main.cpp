#include <iostream>

#include "config.h"
#include "core/platform/paths.hpp"

// StarHaven — open-source engine for Might and Magic VI: The Mandate of Heaven.
//
// This is the foundation slice: a cross-platform C++20/Meson scaffold with a
// verified .LOD archive reader. The launcher reports its version and whether a
// game install is configured; rendering and gameplay come in later slices.
//
// The engine never bundles game data. Point it at your own legal install with
// the STARHAVEN_GAME_DIR environment variable.
int main() {
    std::cout << "StarHaven " << STARHAVEN_VERSION << "\n";

    if (auto install = starhaven::platform::install_from_env()) {
        std::cout << "Game install configured: " << install->string() << "\n";
    } else {
        std::cout << "No game install configured.\n"
                  << "Set " << starhaven::platform::kInstallEnvVar
                  << " to the directory containing MM6.exe and data/.\n";
    }

    if (auto data_dir = starhaven::platform::user_data_dir()) {
        std::cout << "User data dir: " << data_dir->string() << "\n";
    }
    return 0;
}
