#ifndef OPENMM6_CORE_PLATFORM_PATHS_HPP
#define OPENMM6_CORE_PLATFORM_PATHS_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace openmm6::platform {

// The name of the environment variable a user can set to point the engine at
// their own legal game installation. The engine never bundles game data.
inline constexpr const char* kInstallEnvVar = "OPENMM6_GAME_DIR";

// Returns the path a user may have configured through the environment, or
// std::nullopt if unset. The caller validates that the path actually looks
// like a supported install.
[[nodiscard]] std::optional<std::filesystem::path> install_from_env();

// Returns the per-user application-data directory for this engine (creating
// it on first use). The location is platform-appropriate:
//   - Linux:   $XDG_DATA_HOME/openmm6 or ~/.local/share/openmm6
//   - macOS:   ~/Library/Application Support/openmm6
//   - Windows: %LOCALAPPDATA%/openmm6
// Returns std::nullopt if the directory cannot be determined or created.
[[nodiscard]] std::optional<std::filesystem::path> user_data_dir();

}  // namespace openmm6::platform

#endif  // OPENMM6_CORE_PLATFORM_PATHS_HPP
