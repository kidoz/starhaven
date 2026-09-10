#include "game/save_repository.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace starhaven::game;

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = base / ("starhaven-save-test-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("could not create temporary save directory");
    }

    ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

SaveRepository repository(const std::filesystem::path& path) {
    return SaveRepository{path, {{"OutE3.Odm", "New Sorpigal"}, {"D01.blv", "Abandoned Temple"}}};
}

}  // namespace

TEST_CASE("all nine empty save slots are enumerated", "[save][repository]") {
    const TemporaryDirectory temporary;
    const SaveRepository saves = repository(temporary.path());
    const auto slots = saves.inspect_all();

    REQUIRE(slots.size() == 9);
    REQUIRE(saves.path_for_slot(1).filename() == "starhaven.save");
    REQUIRE(saves.path_for_slot(9).filename() == "starhaven-9.save");
    for (std::size_t index = 0; index < slots.size(); ++index) {
        REQUIRE(slots[index].slot == static_cast<int>(index + 1));
        REQUIRE(slots[index].status == SaveSlotStatus::Empty);
    }
}

TEST_CASE("save slots distinguish corrupt unsupported and missing-map files",
          "[save][repository]") {
    const TemporaryDirectory temporary;
    const SaveRepository saves = repository(temporary.path());
    write(saves.path_for_slot(1), "not-a-save\n");
    write(saves.path_for_slot(2), "starhaven-save\t99\nmap\tOutE3.Odm\n");
    SaveState missing_map;
    missing_map.map_file = "Future.odm";
    write(saves.path_for_slot(3), save_text(missing_map));

    REQUIRE(saves.inspect(1).status == SaveSlotStatus::Corrupt);
    REQUIRE(saves.inspect(2).status == SaveSlotStatus::UnsupportedVersion);
    const auto missing = saves.inspect(3);
    REQUIRE(missing.status == SaveSlotStatus::MissingMap);
    REQUIRE(missing.map_file == "Future.odm");
}

TEST_CASE("a valid save exposes map and day metadata and loads atomically", "[save][repository]") {
    const TemporaryDirectory temporary;
    const SaveRepository saves = repository(temporary.path());
    SaveState state;
    state.map_file = "oute3.odm";
    state.minutes = 2 * 24 * 60 + 30;
    state.gold = 1234;
    write(saves.path_for_slot(4), save_text(state));

    const auto info = saves.inspect(4);
    REQUIRE(info.status == SaveSlotStatus::Valid);
    REQUIRE(info.map_name == "New Sorpigal");
    REQUIRE(info.day == 3);

    SaveState loaded;
    loaded.gold = 7;
    REQUIRE(saves.load(4, loaded).loadable());
    REQUIRE(loaded.gold == 1234);

    write(saves.path_for_slot(4), "broken");
    REQUIRE_FALSE(saves.load(4, loaded).loadable());
    REQUIRE(loaded.gold == 1234);
}

TEST_CASE("legacy slot one is read only when the per-user slot is absent", "[save][repository]") {
    const TemporaryDirectory primary;
    const TemporaryDirectory legacy;
    const SaveRepository saves{primary.path(), {{"OutE3.Odm", "New Sorpigal"}}, legacy.path()};
    SaveState old;
    old.map_file = "OutE3.Odm";
    old.gold = 10;
    write(legacy.path() / "starhaven.save", save_text(old));

    SaveState loaded;
    REQUIRE(saves.load(1, loaded).loadable());
    REQUIRE(loaded.gold == 10);

    SaveState current = old;
    current.gold = 20;
    write(saves.path_for_slot(1), save_text(current));
    REQUIRE(saves.load(1, loaded).loadable());
    REQUIRE(loaded.gold == 20);
}

TEST_CASE("a corrupt slot does not prevent inspecting the remaining slots", "[save][repository]") {
    const TemporaryDirectory temporary;
    const SaveRepository saves = repository(temporary.path());
    SaveState valid;
    valid.map_file = "OutE3.Odm";
    valid.gold = 42;
    write(saves.path_for_slot(2), save_text(valid));

    for (const std::string bad : {"broken", "12oops", "999999999999999999999999"}) {
        INFO(bad);
        std::string text = save_text(valid);
        const auto start = text.find("gold\t");
        text.replace(start, text.find('\n', start) - start, "gold\t" + bad + "\t0");
        write(saves.path_for_slot(1), text);
        const auto slots = saves.inspect_all();
        REQUIRE(slots[0].status == SaveSlotStatus::Corrupt);
        REQUIRE(slots[1].loadable());
        SaveState unchanged = valid;
        REQUIRE_FALSE(saves.load(1, unchanged).loadable());
        REQUIRE(unchanged.gold == 42);
    }
}

TEST_CASE("a header and map alone are not a complete save", "[save][repository]") {
    const TemporaryDirectory temporary;
    const SaveRepository saves = repository(temporary.path());
    write(saves.path_for_slot(1), "starhaven-save\t1\nmap\tOutE3.Odm\n");
    REQUIRE(saves.inspect(1).status == SaveSlotStatus::Corrupt);
}
