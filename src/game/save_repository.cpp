#include "game/save_repository.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace starhaven::game {
namespace {

[[nodiscard]] bool equal_ascii_case(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
           });
}

[[nodiscard]] bool read_version(std::string_view text, int& version) {
    const auto end = text.find('\n');
    std::istringstream header(std::string{text.substr(0, end)});
    std::string magic;
    return static_cast<bool>(header >> magic >> version) && magic == kSaveMagic;
}

}  // namespace

SaveRepository::SaveRepository(std::filesystem::path directory, std::vector<SaveMapInfo> maps,
                               std::optional<std::filesystem::path> legacy_directory)
    : directory_(std::move(directory)), maps_(std::move(maps)),
      legacy_directory_(std::move(legacy_directory)) {}

std::filesystem::path SaveRepository::path_for_slot(int slot) const {
    if (slot <= 1) {
        return directory_ / "starhaven.save";
    }
    return directory_ / ("starhaven-" + std::to_string(slot) + ".save");
}

std::filesystem::path SaveRepository::path_for_read(int slot) const {
    const std::filesystem::path primary = path_for_slot(slot);
    std::error_code error;
    const bool primary_exists = std::filesystem::exists(primary, error);
    if (slot != 1 || !legacy_directory_ || error || primary_exists) {
        return primary;
    }
    return *legacy_directory_ / "starhaven.save";
}

const SaveMapInfo* SaveRepository::find_map(std::string_view file_name) const noexcept {
    const auto found = std::find_if(maps_.begin(), maps_.end(), [&](const SaveMapInfo& map) {
        return equal_ascii_case(map.file_name, file_name);
    });
    return found == maps_.end() ? nullptr : &*found;
}

SaveSlotInfo SaveRepository::load(int slot, SaveState& out) const {
    SaveSlotInfo info;
    info.slot = slot;
    if (slot < 1 || slot > kSaveSlotCount) {
        info.status = SaveSlotStatus::Corrupt;
        info.message = "Invalid slot number";
        return info;
    }

    std::ifstream file(path_for_read(slot), std::ios::binary);
    if (!file.is_open()) {
        info.status = SaveSlotStatus::Empty;
        info.message = "Empty slot";
        return info;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        info.status = SaveSlotStatus::Corrupt;
        info.message = "Save could not be read";
        return info;
    }
    const std::string text = buffer.str();
    int version = 0;
    if (!read_version(text, version)) {
        info.status = SaveSlotStatus::Corrupt;
        info.message = "Not a StarHaven save";
        return info;
    }
    if (version != kSaveVersion) {
        info.status = SaveSlotStatus::UnsupportedVersion;
        info.message = "Unsupported save version " + std::to_string(version);
        return info;
    }

    SaveState candidate;
    if (!parse_save(text, candidate)) {
        info.status = SaveSlotStatus::Corrupt;
        info.message = "Save data is corrupt";
        return info;
    }
    info.map_file = candidate.map_file;
    const SaveMapInfo* map = find_map(candidate.map_file);
    if (map == nullptr) {
        info.status = SaveSlotStatus::MissingMap;
        info.map_name = candidate.map_file;
        info.message = "Map is not present in this installation";
        return info;
    }

    info.status = SaveSlotStatus::Valid;
    info.map_name = map->display_name.empty() ? map->file_name : map->display_name;
    info.day = candidate.minutes / (24 * 60) + 1;
    info.message = "Ready to load";
    out = std::move(candidate);
    return info;
}

SaveSlotInfo SaveRepository::inspect(int slot) const {
    SaveState ignored;
    return load(slot, ignored);
}

std::array<SaveSlotInfo, kSaveSlotCount> SaveRepository::inspect_all() const {
    std::array<SaveSlotInfo, kSaveSlotCount> slots;
    for (int slot = 1; slot <= kSaveSlotCount; ++slot) {
        slots[static_cast<std::size_t>(slot - 1)] = inspect(slot);
    }
    return slots;
}

std::string_view save_slot_status_name(SaveSlotStatus status) noexcept {
    switch (status) {
    case SaveSlotStatus::Empty:
        return "empty";
    case SaveSlotStatus::Corrupt:
        return "corrupt";
    case SaveSlotStatus::UnsupportedVersion:
        return "unsupported";
    case SaveSlotStatus::MissingMap:
        return "missing map";
    case SaveSlotStatus::Valid:
        return "valid";
    }
    return "unknown";
}

}  // namespace starhaven::game
