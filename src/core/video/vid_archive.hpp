#ifndef STARHAVEN_CORE_VIDEO_VID_ARCHIVE_HPP
#define STARHAVEN_CORE_VIDEO_VID_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::video {

// Outcome of parsing a `.vid` archive. Callers convert these into user-facing
// text; the reader never throws.
enum class VidError {
    None,
    // File could not be opened or read.
    Io,
    // Fewer bytes than the 4-byte entry count needs.
    TooSmall,
    // The entry count is zero, or its directory would not fit in the file.
    BadCount,
    // An entry's offset lies outside the file, or offsets are not ascending.
    BadOffset,
};

// One directory entry: a named video and where its bytes live in the file.
// `size` is derived from the next entry's offset (or the file end), because the
// directory stores only offsets.
struct VidEntry {
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// Reader for the `.vid` video container (`Anims1.vid`, `Anims2.vid`).
// See docs/formats/vid.md.
//
// Unlike LodArchive, this keeps the file open and reads one entry on demand
// rather than holding the whole archive in memory: MM6 ships a 208 MB `.vid`,
// and callers want one video at a time.
class VidArchive {
public:
    VidArchive() = default;

    // Open a file and read its directory.
    [[nodiscard]] static VidError open(const std::filesystem::path& path, VidArchive& out);

    // Parse a directory from an in-memory buffer (used by tests with synthetic
    // fixtures). The buffer is copied, so `read` works without a file.
    [[nodiscard]] static VidError parse(std::vector<std::byte> data, VidArchive& out);

    [[nodiscard]] const std::vector<VidEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Find an entry by name, case-insensitively. Returns entries().size() when
    // there is no such entry.
    [[nodiscard]] std::size_t find(std::string_view name) const noexcept;

    // Read one entry's bytes. Returns false if the index is out of range or the
    // read fails.
    [[nodiscard]] bool read(std::size_t index, std::vector<std::byte>& out);

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<std::byte> memory_;  // populated only by parse()
    bool in_memory_ = false;
    std::uint64_t file_size_ = 0;
    std::vector<VidEntry> entries_;

    [[nodiscard]] static VidError read_directory(std::span<const std::byte> header,
                                                 std::uint64_t file_size,
                                                 std::vector<VidEntry>& out);
};

// Directory layout constants (see docs/formats/vid.md).
constexpr std::uint32_t kVidNameSize = 40;
constexpr std::uint32_t kVidEntrySize = 44;  // name[40] + u32 offset

}  // namespace starhaven::video

#endif  // STARHAVEN_CORE_VIDEO_VID_ARCHIVE_HPP
