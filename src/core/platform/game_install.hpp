#ifndef STARHAVEN_CORE_PLATFORM_GAME_INSTALL_HPP
#define STARHAVEN_CORE_PLATFORM_GAME_INSTALL_HPP

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace starhaven::platform {

inline constexpr const char* kInstallSettingFile = "game-install.conf";
inline constexpr std::array<const char*, 4> kRequiredGameArchives = {"BITMAPS.LOD", "SPRITES.LOD",
                                                                     "icons.lod", "Games.lod"};

enum class InstallProblem {
    None,
    MissingPath,
    PathDoesNotExist,
    NotDirectory,
    MissingDataDirectory,
    MissingArchive,
    ArchiveNotFile,
    UnreadableArchive,
};

enum class InstallSource {
    None,
    CommandLine,
    PersistedSetting,
    Environment,
    FolderSelection,
};

struct GameInstall {
    std::filesystem::path root;
    std::filesystem::path data_directory;
};

struct InstallValidation {
    InstallProblem problem = InstallProblem::MissingPath;
    GameInstall install;
    std::string requirement;

    [[nodiscard]] bool valid() const noexcept { return problem == InstallProblem::None; }
};

struct InstallCandidates {
    std::optional<std::filesystem::path> command_line;
    std::optional<std::filesystem::path> persisted_setting;
    std::optional<std::filesystem::path> environment;
};

struct InstallResolution {
    InstallSource source = InstallSource::None;
    InstallValidation validation;

    [[nodiscard]] bool valid() const noexcept { return validation.valid(); }
};

// Validate a user-owned installation without writing to it. A caller may pass
// either the installation root or its Data directory.
[[nodiscard]] InstallValidation validate_game_install(const std::filesystem::path& candidate);

// Select the first valid candidate in command-line, persisted-setting, then
// environment order. If all configured candidates fail, return the first
// failure so the highest-priority diagnostic remains actionable.
[[nodiscard]] InstallResolution resolve_game_install(const InstallCandidates& candidates);

// The setting contains only a schema marker, the validation contract, and the
// selected path. These helpers never inspect or copy game data.
[[nodiscard]] std::optional<std::filesystem::path>
load_game_install_setting(const std::filesystem::path& settings_directory);
[[nodiscard]] bool persist_game_install_setting(const std::filesystem::path& settings_directory,
                                                const GameInstall& install);

[[nodiscard]] std::string install_problem_message(const InstallValidation& validation);
[[nodiscard]] const char* install_source_name(InstallSource source) noexcept;

}  // namespace starhaven::platform

#endif  // STARHAVEN_CORE_PLATFORM_GAME_INSTALL_HPP
