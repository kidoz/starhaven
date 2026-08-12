// Hermetic tests for MM6 installation discovery. Fixtures contain only tiny
// synthetic placeholder files; no game data is read or copied.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/platform/game_install.hpp"

using namespace starhaven::platform;

namespace {

namespace fs = std::filesystem;

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("starhaven-install-test-" + std::to_string(stamp) +
                                             "-" + std::to_string(sequence.fetch_add(1)));
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

[[nodiscard]] fs::path make_install(const fs::path& root) {
    const fs::path data = root / "Data";
    fs::create_directories(data);
    for (const char* archive : kRequiredGameArchives) {
        std::ofstream{data / archive, std::ios::binary} << "synthetic";
    }
    return root;
}

}  // namespace

TEST_CASE("missing and invalid installation paths are diagnosed", "[game_install]") {
    TempDirectory temporary;

    REQUIRE(validate_game_install({}).problem == InstallProblem::MissingPath);
    REQUIRE(validate_game_install(temporary.path() / "absent").problem ==
            InstallProblem::PathDoesNotExist);

    const fs::path file = temporary.path() / "not-a-folder";
    std::ofstream{file} << "synthetic";
    REQUIRE(validate_game_install(file).problem == InstallProblem::NotDirectory);

    const fs::path wrong = temporary.path() / "wrong-folder";
    fs::create_directories(wrong);
    REQUIRE(validate_game_install(wrong).problem == InstallProblem::MissingDataDirectory);
}

TEST_CASE("required archive failures name the requirement", "[game_install]") {
    TempDirectory temporary;
    const fs::path root = temporary.path() / "install";
    REQUIRE(make_install(root) == root);

    fs::remove(root / "Data" / "SPRITES.LOD");
    InstallValidation validation = validate_game_install(root);
    REQUIRE(validation.problem == InstallProblem::MissingArchive);
    REQUIRE(validation.requirement == "SPRITES.LOD");

    fs::create_directory(root / "Data" / "SPRITES.LOD");
    validation = validate_game_install(root);
    REQUIRE(validation.problem == InstallProblem::ArchiveNotFile);
    REQUIRE(validation.requirement == "SPRITES.LOD");
}

#if !defined(_WIN32)
TEST_CASE("an unreadable required archive is rejected", "[game_install]") {
    TempDirectory temporary;
    const fs::path root = make_install(temporary.path() / "install");
    const fs::path archive = root / "Data" / "icons.lod";
    fs::permissions(archive, fs::perms::none);

    const InstallValidation validation = validate_game_install(root);

    fs::permissions(archive, fs::perms::owner_all);
    REQUIRE(validation.problem == InstallProblem::UnreadableArchive);
    REQUIRE(validation.requirement == "icons.lod");
}
#endif

TEST_CASE("a supported installation and its Data folder both validate", "[game_install]") {
    TempDirectory temporary;
    const fs::path root = make_install(temporary.path() / "MM6 install");

    const InstallValidation from_root = validate_game_install(root);
    REQUIRE(from_root.valid());
    REQUIRE(from_root.install.root == root);
    REQUIRE(from_root.install.data_directory == root / "Data");

    const InstallValidation from_data = validate_game_install(root / "Data");
    REQUIRE(from_data.valid());
    REQUIRE(from_data.install.root == root);
}

TEST_CASE("non-ASCII installation paths remain usable", "[game_install]") {
    TempDirectory temporary;
    const fs::path root = make_install(temporary.path() / fs::path{u8"Игры 六"});

    const InstallValidation validation = validate_game_install(root);

    REQUIRE(validation.valid());
    REQUIRE(validation.install.root == root);
}

TEST_CASE("install candidates resolve in declared precedence order", "[game_install]") {
    TempDirectory temporary;
    const fs::path command_line = make_install(temporary.path() / "command-line");
    const fs::path persisted = make_install(temporary.path() / "persisted");
    const fs::path environment = make_install(temporary.path() / "environment");

    InstallCandidates candidates{command_line, persisted, environment};
    InstallResolution resolution = resolve_game_install(candidates);
    REQUIRE(resolution.valid());
    REQUIRE(resolution.source == InstallSource::CommandLine);
    REQUIRE(resolution.validation.install.root == command_line);

    fs::remove(command_line / "Data" / "Games.lod");
    resolution = resolve_game_install(candidates);
    REQUIRE(resolution.valid());
    REQUIRE(resolution.source == InstallSource::PersistedSetting);

    fs::remove(persisted / "Data" / "Games.lod");
    resolution = resolve_game_install(candidates);
    REQUIRE(resolution.valid());
    REQUIRE(resolution.source == InstallSource::Environment);
}

TEST_CASE("persisted install setting round-trips only validation metadata and path",
          "[game_install]") {
    TempDirectory temporary;
    const fs::path root = make_install(temporary.path() / fs::path{u8"Saved Игры"});
    const InstallValidation validation = validate_game_install(root);
    REQUIRE(validation.valid());

    const fs::path settings = temporary.path() / "settings";
    REQUIRE(persist_game_install_setting(settings, validation.install));
    const auto loaded = load_game_install_setting(settings);

    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == root);
    REQUIRE(validate_game_install(*loaded).valid());
}
