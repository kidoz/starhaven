#ifndef STARHAVEN_CORE_AUDIO_SND_ARCHIVE_HPP
#define STARHAVEN_CORE_AUDIO_SND_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starhaven::audio {

// Outcome of reading a `.snd` sound archive. The reader never throws.
enum class SndError {
    None,
    // File could not be opened or read.
    Io,
    // Fewer bytes than the 4-byte entry count needs.
    TooSmall,
    // The entry count is zero, or its directory would not fit in the file.
    BadCount,
    // An entry's bytes lie outside the file.
    BadOffset,
    // An entry's stored bytes did not inflate.
    InflateFailed,
};

// One directory entry.
struct SndEntry {
    std::string name;
    std::uint64_t offset = 0;
    std::uint32_t packed_size = 0;
    std::uint32_t unpacked_size = 0;

    // Entries whose two sizes agree are stored as-is rather than compressed.
    [[nodiscard]] bool stored() const noexcept { return packed_size == unpacked_size; }
};

// Reader for `Sounds/Audio.snd`, the sound-effect archive.
// See docs/formats/snd.md.
//
// Like VidArchive, this keeps the file open and reads one entry at a time: the
// archive is 18 MB and callers want a single effect.
class SndArchive {
public:
    SndArchive() = default;

    [[nodiscard]] static SndError open(const std::filesystem::path& path,
                                       SndArchive& out);

    // Parse a directory from memory (used by tests with synthetic fixtures).
    [[nodiscard]] static SndError parse(std::vector<std::byte> data,
                                        SndArchive& out);

    [[nodiscard]] const std::vector<SndEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Find an entry by name, case-insensitively; entries().size() if absent.
    [[nodiscard]] std::size_t find(std::string_view name) const noexcept;

    // Read one entry, inflating it when it is compressed. The result is the
    // entry's RIFF/WAVE bytes.
    [[nodiscard]] SndError read(std::size_t index, std::vector<std::uint8_t>& out);

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<std::byte> memory_;
    bool in_memory_ = false;
    std::uint64_t file_size_ = 0;
    std::vector<SndEntry> entries_;

    [[nodiscard]] static SndError read_directory(std::span<const std::byte> header,
                                                 std::uint64_t file_size,
                                                 std::vector<SndEntry>& out);
};

constexpr std::uint32_t kSndNameSize = 40;
constexpr std::uint32_t kSndEntrySize = 52;  // name[40] + 3 x u32

}  // namespace starhaven::audio

#endif  // STARHAVEN_CORE_AUDIO_SND_ARCHIVE_HPP
