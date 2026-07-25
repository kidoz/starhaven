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

#if defined(__APPLE__)
    if (auto home = from_env_var("HOME")) {
        return *home / "Library" / "Application Support" / kAppName;
    }
#elif defined(_WIN32)
    if (auto local = from_env_var("LOCALAPPDATA")) {
        return *local / kAppName;
    }
#else
    if (auto xdg = from_env_var("XDG_DATA_HOME")) {
        return *xdg / kAppName;
    }
    if (auto home = from_env_var("HOME")) {
        return *home / ".local" / "share" / kAppName;
    }
#endif

    // Could not determine a home directory; the caller should report that the
    // install path must be supplied explicitly.
    return std::nullopt;
}

}  // namespace starhaven::platform
