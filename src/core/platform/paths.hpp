#ifndef STARHAVEN_CORE_PLATFORM_PATHS_HPP
#define STARHAVEN_CORE_PLATFORM_PATHS_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace starhaven::platform {

// The name of the environment variable a user can set to point the engine at
// their own legal game installation. The engine never bundles game data.
inline constexpr const char* kInstallEnvVar = "STARHAVEN_GAME_DIR";

// Returns the path a user may have configured through the environment, or
// std::nullopt if unset. The caller validates that the path actually looks
// like a supported install.
[[nodiscard]] std::optional<std::filesystem::path> install_from_env();

// Returns the per-user application-data directory for this engine (creating
// it on first use). The location is platform-appropriate:
//   - Linux:   $XDG_DATA_HOME/starhaven or ~/.local/share/starhaven
//   - macOS:   ~/Library/Application Support/starhaven
//   - Windows: %LOCALAPPDATA%/starhaven
// Returns std::nullopt if the directory cannot be determined or created.
[[nodiscard]] std::optional<std::filesystem::path> user_data_dir();

}  // namespace starhaven::platform

#endif  // STARHAVEN_CORE_PLATFORM_PATHS_HPP
