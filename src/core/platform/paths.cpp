#include "core/platform/paths.hpp"

#include <cstdlib>
#include <system_error>

namespace starhaven::platform {

namespace {

constexpr const char* kAppName = "starhaven";

[[nodiscard]] std::optional<std::filesystem::path> from_env_var(const char* name) {
    if (const char* value = std::getenv(name)) {
        if (value[0] != '\0') {
            return std::filesystem::path{value};
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::filesystem::path> install_from_env() {
    return from_env_var(kInstallEnvVar);
}

std::optional<std::filesystem::path> user_data_dir() {
    namespace fs = std::filesystem;

    std::optional<fs::path> directory;

#if defined(__APPLE__)
    if (auto home = from_env_var("HOME")) {
        directory = *home / "Library" / "Application Support" / kAppName;
    }
#elif defined(_WIN32)
    if (auto local = from_env_var("LOCALAPPDATA")) {
        directory = *local / kAppName;
    }
#else
    if (auto xdg = from_env_var("XDG_DATA_HOME")) {
        directory = *xdg / kAppName;
    } else if (auto home = from_env_var("HOME")) {
        directory = *home / ".local" / "share" / kAppName;
    }
#endif

    if (!directory) {
        // Could not determine a home directory; the caller should report that
        // the install path must be supplied explicitly.
        return std::nullopt;
    }
    std::error_code ec;
    fs::create_directories(*directory, ec);
    return ec ? std::nullopt : directory;
}

}  // namespace starhaven::platform
