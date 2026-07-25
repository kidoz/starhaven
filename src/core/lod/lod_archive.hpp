#ifndef STARHAVEN_CORE_LOD_LOD_ARCHIVE_HPP
#define STARHAVEN_CORE_LOD_LOD_ARCHIVE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace starhaven::lod {

// The resource-archive variants distinguishable from the header. Only the MM6
// standard format is parsed today; other variants are reported explicitly.
enum class LodKind {
    Bitmaps,  // lod_type "bitmaps" — BMP/PCX images and palettes
    Sprites,  // lod_type "sprites"
    Icons,    // lod_type "icons"
    Game,     // lod_type "game" — Games.lod live-game-data (NOT yet supported)
    Unknown,  // any other lod_type
};

// Outcome of parsing an archive. Callers convert these into user-facing text;
// the reader never throws.
enum class LodError {
    None,
    // File could not be opened or read.
    Io,
    // Buffer too small to contain the 288-byte MM6 header.
    TooSmall,
    // Magic is not "LOD\0".
    BadMagic,
    // Version code at offset 4 is the Heroes III form (<= 0xFFFF); unsupported.
    UnsupportedVersion,
    // Version string is not a recognized MM-family marker.
    UnknownVersion,
    // archive_start points outside the file.
    BadArchiveStart,
    // count would push the directory table past end-of-file.
    CountTooLarge,
    // An entry's data offset is out of range.
    EntryOffsetOutOfRange,
    // An entry's gap-derived size would read past end-of-file.
    EntrySizeOutOfRange,
    // The lod_type field did not match a known archive name.
    UnknownLodType,
};

// One directory entry. `data_offset` and `stored_size` locate the raw bytes of
// the entry inside the file; `uncompressed` tells whether those bytes are the
// payload directly (true) or compressed data (false).
struct LodEntry {
    std::string name;
    std::uint64_t data_offset = 0;  // absolute file offset of the stored bytes
    std::uint32_t stored_size = 0;  // bytes occupied in the file
    std::uint32_t unpacked_size = 0;
    bool uncompressed = true;  // unpacked_size == 0
};

// Parsed view of a standard MM6 `.LOD` archive. Holds an in-memory copy of the
// file bytes (MM6 archives are tens of megabytes, which is acceptable for a
// browser/loader; a streaming/mmap variant can come later).
class LodArchive {
public:
    LodArchive() = default;

    // Parse a file from disk. Returns the error (None on success).
    [[nodiscard]] static LodError open(const std::filesystem::path& path, LodArchive& out);

    // Parse an in-memory buffer (used by tests with synthetic fixtures).
    [[nodiscard]] static LodError parse(std::vector<std::byte> data, LodArchive& out);

    [[nodiscard]] LodKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& version() const noexcept { return version_; }
    [[nodiscard]] const std::string& lod_type() const noexcept { return lod_type_; }
    [[nodiscard]] std::uint32_t archive_start() const noexcept { return archive_start_; }
    [[nodiscard]] std::uint16_t count() const noexcept { return count_; }
    [[nodiscard]] const std::vector<LodEntry>& entries() const noexcept { return entries_; }

    // Look up an entry by name (case-insensitive, as archive names are mixed-case).
    [[nodiscard]] std::optional<LodEntry> find(std::string_view name) const;

    // Obtain the raw stored bytes for an entry. For an uncompressed entry this
    // is the payload; for a compressed entry it is the compressed bytes.
    // Returns the error Unsupported (entry is compressed) — payload decoding is
    // a follow-up slice. `out` is a span into the archive's own buffer.
    enum class PayloadError { None, NotFound, Compressed, OutOfRange };
    [[nodiscard]] PayloadError payload(std::string_view name,
                                       std::span<const std::byte>& out) const;

private:
    [[nodiscard]] LodError parse_impl();

    std::vector<std::byte> data_;
    LodKind kind_ = LodKind::Unknown;
    std::string version_;
    std::string lod_type_;
    std::uint32_t archive_start_ = 0;
    std::uint16_t count_ = 0;
    std::vector<LodEntry> entries_;
};

}  // namespace starhaven::lod

#endif  // STARHAVEN_CORE_LOD_LOD_ARCHIVE_HPP
