#ifndef STARHAVEN_GAME_SAVE_REPOSITORY_HPP
#define STARHAVEN_GAME_SAVE_REPOSITORY_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/save.hpp"

namespace starhaven::game {

inline constexpr int kSaveSlotCount = 9;

enum class SaveSlotStatus : std::uint8_t {
    Empty,
    Corrupt,
    UnsupportedVersion,
    MissingMap,
    Valid,
};

struct SaveMapInfo {
    std::string file_name;
    std::string display_name;
};

struct SaveSlotInfo {
    int slot = 1;
    SaveSlotStatus status = SaveSlotStatus::Empty;
    std::string map_file;
    std::string map_name;
    std::int64_t day = 0;
    std::string message;

    [[nodiscard]] bool loadable() const noexcept { return status == SaveSlotStatus::Valid; }
};

// Owns no game resources. It only validates StarHaven's versioned text saves
// against the map names already read from the user's installation.
class SaveRepository {
public:
    SaveRepository(std::filesystem::path directory, std::vector<SaveMapInfo> maps,
                   std::optional<std::filesystem::path> legacy_directory = std::nullopt);

    [[nodiscard]] std::filesystem::path path_for_slot(int slot) const;
    [[nodiscard]] SaveSlotInfo inspect(int slot) const;
    [[nodiscard]] std::array<SaveSlotInfo, kSaveSlotCount> inspect_all() const;

    // On failure `out` is untouched, so a menu cannot accidentally expose a
    // partial or default session.
    [[nodiscard]] SaveSlotInfo load(int slot, SaveState& out) const;

private:
    [[nodiscard]] const SaveMapInfo* find_map(std::string_view file_name) const noexcept;
    [[nodiscard]] std::filesystem::path path_for_read(int slot) const;

    std::filesystem::path directory_;
    std::vector<SaveMapInfo> maps_;
    std::optional<std::filesystem::path> legacy_directory_;
};

[[nodiscard]] std::string_view save_slot_status_name(SaveSlotStatus status) noexcept;

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SAVE_REPOSITORY_HPP
