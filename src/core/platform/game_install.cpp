#include "core/platform/game_install.hpp"

#include <fstream>
#include <string_view>
#include <system_error>

namespace starhaven::platform {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool same_ascii_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto lower = [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
        };
        if (lower(static_cast<unsigned char>(left[i])) !=
            lower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<fs::path> child_named(const fs::path& directory,
                                                  std::string_view wanted) {
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (same_ascii_case(name, wanted)) {
            return it->path();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] fs::path path_from_utf8(std::string_view encoded) {
    const auto* begin = reinterpret_cast<const char8_t*>(encoded.data());
    return fs::path{std::u8string{begin, begin + encoded.size()}};
}

[[nodiscard]] InstallResolution resolution_for(InstallSource source, const fs::path& path) {
    return {source, validate_game_install(path)};
}

}  // namespace

InstallValidation validate_game_install(const fs::path& candidate) {
    if (candidate.empty()) {
        return {InstallProblem::MissingPath, {}, "installation directory"};
    }

    std::error_code ec;
    if (!fs::exists(candidate, ec) || ec) {
        return {InstallProblem::PathDoesNotExist, {}, path_to_utf8(candidate)};
    }
    if (!fs::is_directory(candidate, ec) || ec) {
        return {InstallProblem::NotDirectory, {}, path_to_utf8(candidate)};
    }

    fs::path absolute_candidate = fs::absolute(candidate, ec);
    if (ec) {
        absolute_candidate = candidate;
        ec.clear();
    }
    absolute_candidate = absolute_candidate.lexically_normal();

    fs::path root = absolute_candidate;
    fs::path data_directory;
    if (same_ascii_case(absolute_candidate.filename().string(), "data")) {
        data_directory = absolute_candidate;
        root = absolute_candidate.parent_path();
    } else if (const auto data = child_named(absolute_candidate, "data")) {
        data_directory = *data;
    } else {
        return {InstallProblem::MissingDataDirectory, {}, "Data"};
    }

    for (const char* required : kRequiredGameArchives) {
        const fs::path archive = data_directory / required;
        if (!fs::exists(archive, ec) || ec) {
            return {InstallProblem::MissingArchive, {root, data_directory}, required};
        }
        if (!fs::is_regular_file(archive, ec) || ec) {
            return {InstallProblem::ArchiveNotFile, {root, data_directory}, required};
        }
        std::ifstream input(archive, std::ios::binary);
        char byte = 0;
        input.read(&byte, 1);
        if (!input && !input.eof()) {
            return {InstallProblem::UnreadableArchive, {root, data_directory}, required};
        }
    }

    return {InstallProblem::None, {root, data_directory}, {}};
}

InstallResolution resolve_game_install(const InstallCandidates& candidates) {
    std::optional<InstallResolution> first_failure;
    const auto try_candidate =
        [&](InstallSource source,
            const std::optional<fs::path>& path) -> std::optional<InstallResolution> {
        if (!path || path->empty()) {
            return std::nullopt;
        }
        InstallResolution result = resolution_for(source, *path);
        if (!first_failure) {
            first_failure = result;
        }
        return result;
    };

    if (auto result = try_candidate(InstallSource::CommandLine, candidates.command_line);
        result && result->valid()) {
        return *result;
    }
    if (auto result = try_candidate(InstallSource::PersistedSetting, candidates.persisted_setting);
        result && result->valid()) {
        return *result;
    }
    if (auto result = try_candidate(InstallSource::Environment, candidates.environment);
        result && result->valid()) {
        return *result;
    }
    if (first_failure) {
        return *first_failure;
    }
    return {InstallSource::None, {InstallProblem::MissingPath, {}, "installation directory"}};
}

std::optional<fs::path> load_game_install_setting(const fs::path& settings_directory) {
    std::ifstream input(settings_directory / kInstallSettingFile, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view kPrefix = "root_utf8=";
        if (line.starts_with(kPrefix) && line.size() > kPrefix.size()) {
            return path_from_utf8(std::string_view{line}.substr(kPrefix.size()));
        }
    }
    return std::nullopt;
}

bool persist_game_install_setting(const fs::path& settings_directory, const GameInstall& install) {
    std::error_code ec;
    fs::create_directories(settings_directory, ec);
    if (ec) {
        return false;
    }

    const fs::path setting = settings_directory / kInstallSettingFile;
    const fs::path temporary = settings_directory / (std::string{kInstallSettingFile} + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << "schema=1\n"
               << "validated=BITMAPS.LOD,SPRITES.LOD,icons.lod,Games.lod\n"
               << "root_utf8=" << path_to_utf8(install.root) << '\n';
        if (!output) {
            return false;
        }
    }

    fs::rename(temporary, setting, ec);
    if (!ec) {
        return true;
    }
    // Windows does not replace an existing destination through rename.
    ec.clear();
    fs::remove(setting, ec);
    ec.clear();
    fs::rename(temporary, setting, ec);
    return !ec;
}

std::string install_problem_message(const InstallValidation& validation) {
    switch (validation.problem) {
    case InstallProblem::None:
        return "MM6 installation is ready.";
    case InstallProblem::MissingPath:
        return "Choose your Might and Magic VI installation folder.";
    case InstallProblem::PathDoesNotExist:
        return "The configured installation path does not exist.";
    case InstallProblem::NotDirectory:
        return "The configured installation path is not a folder.";
    case InstallProblem::MissingDataDirectory:
        return "The selected folder does not contain the required Data folder.";
    case InstallProblem::MissingArchive:
        return "The selected installation is missing " + validation.requirement + ".";
    case InstallProblem::ArchiveNotFile:
        return validation.requirement + " is not a regular archive file.";
    case InstallProblem::UnreadableArchive:
        return "The selected installation cannot read " + validation.requirement + ".";
    }
    return "The selected installation is not supported.";
}

const char* install_source_name(InstallSource source) noexcept {
    switch (source) {
    case InstallSource::None:
        return "none";
    case InstallSource::CommandLine:
        return "--game-dir";
    case InstallSource::PersistedSetting:
        return "saved setting";
    case InstallSource::Environment:
        return "STARHAVEN_GAME_DIR";
    case InstallSource::FolderSelection:
        return "folder selection";
    }
    return "none";
}

}  // namespace starhaven::platform
