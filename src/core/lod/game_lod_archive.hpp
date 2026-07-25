#ifndef STARHAVEN_CORE_LOD_GAME_LOD_ARCHIVE_HPP
#define STARHAVEN_CORE_LOD_GAME_LOD_ARCHIVE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace starhaven::lod {

// Outcome of parsing a Games.lod container. Callers convert these into
// user-facing text; the reader never throws.
enum class GameLodError {
    None,
    Io,
    // Buffer too small for the 256-byte header + 32-byte root entry.
    TooSmall,
    // Signature is not "LOD\0".
    BadSignature,
    // Version is not a "Game*" form (e.g. "GameMMVI").
    UnsupportedVersion,
    // The header claims a directory count other than 1.
    UnsupportedDirectoryCount,
    // Root entry dataOffset is out of range.
    BadRootOffset,
    // The file-entry table would extend past the root's data region.
    TableTooLarge,
    // An entry's data would extend past the root's data region.
    EntryOutOfRange,
};

// One file entry inside Games.lod.
struct GameLodEntry {
    std::string name;
    std::uint64_t data_offset = 0;  // absolute file offset of the entry's bytes
    std::uint32_t data_size = 0;    // raw byte count (no container compression)
};

// Parsed view of a Games.lod container. Holds an in-memory copy of the file
// bytes (~10 MB, acceptable for a loader). The contained `.blv`/`.odm`/`.dlv`/
// `.ddm` files are stored raw; their internals are a separate concern.
class GameLodArchive {
public:
    GameLodArchive() = default;

    [[nodiscard]] static GameLodError open(const std::filesystem::path& path,
                                           GameLodArchive& out);
    [[nodiscard]] static GameLodError parse(std::vector<std::byte> data,
                                            GameLodArchive& out);

    [[nodiscard]] const std::string& version() const noexcept { return version_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] const std::string& root_name() const noexcept { return root_name_; }
    [[nodiscard]] std::uint32_t root_data_offset() const noexcept { return root_data_offset_; }
    [[nodiscard]] std::uint16_t num_items() const noexcept { return num_items_; }
    [[nodiscard]] const std::vector<GameLodEntry>& entries() const noexcept { return entries_; }

    [[nodiscard]] std::optional<GameLodEntry> find(std::string_view name) const;

    // Obtain the raw bytes of an entry as a span into the archive's own buffer.
    enum class PayloadError { None, NotFound, OutOfRange };
    [[nodiscard]] PayloadError payload(std::string_view name,
                                       std::span<const std::byte>& out) const;

private:
    [[nodiscard]] GameLodError parse_impl();

    std::vector<std::byte> data_;
    std::string version_;
    std::string description_;
    std::string root_name_;
    std::uint32_t root_data_offset_ = 0;
    std::uint16_t num_items_ = 0;
    std::vector<GameLodEntry> entries_;
};

}  // namespace starhaven::lod

#endif  // STARHAVEN_CORE_LOD_GAME_LOD_ARCHIVE_HPP
