#include "core/lod/game_lod_archive.hpp"

#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <ranges>
#include <system_error>

namespace starhaven::lod {

namespace {

// Verified layout constants (see docs/formats/games-lod.md).
constexpr std::uint32_t kHeaderSize = 256;
constexpr std::uint32_t kRootEntryOffset = 256;  // LodEntry_MM6 right after header
constexpr std::uint32_t kRootEntrySize = 32;
constexpr std::uint32_t kFileEntrySize = 32;
constexpr std::uint32_t kEntryNameSize = 16;

constexpr std::uint32_t kHdrSignatureOff = 0x00;
constexpr std::uint32_t kHdrVersionOff = 0x04;
constexpr std::uint32_t kHdrVersionSize = 80;
constexpr std::uint32_t kHdrDescriptionOff = 0x54;
constexpr std::uint32_t kHdrDescriptionSize = 80;
constexpr std::uint32_t kHdrNumDirsOff = 0xAC;

constexpr std::uint32_t kEntryDataOff = 0x10;
constexpr std::uint32_t kEntryDataSize = 0x14;
constexpr std::uint32_t kEntryNumItemsOff = 0x1C;

[[nodiscard]] GameLodError read_file(const std::filesystem::path& path,
                                     std::vector<std::byte>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return GameLodError::Io;
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return GameLodError::Io;
    }
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (!f) {
            return GameLodError::Io;
        }
    }
    return GameLodError::None;
}

}  // namespace

GameLodError GameLodArchive::open(const std::filesystem::path& path, GameLodArchive& out) {
    std::vector<std::byte> data;
    if (GameLodError e = read_file(path, data); e != GameLodError::None) {
        return e;
    }
    return GameLodArchive::parse(std::move(data), out);
}

GameLodError GameLodArchive::parse(std::vector<std::byte> data, GameLodArchive& out) {
    out = GameLodArchive{};
    out.data_ = std::move(data);
    return out.parse_impl();
}

GameLodError GameLodArchive::parse_impl() {
    if (data_.size() < kHeaderSize + kRootEntrySize) {
        return GameLodError::TooSmall;
    }

    io::ByteReader r{std::span<const std::byte>{data_}};

    // Signature "LOD\0".
    if (!r.seek(kHdrSignatureOff)) {
        return GameLodError::TooSmall;
    }
    if (r.read_u8() != 'L' || r.read_u8() != 'O' || r.read_u8() != 'D' || r.read_u8() != 0) {
        return GameLodError::BadSignature;
    }

    // Version string at offset 4 (80 bytes). Games.lod uses a "Game*" form.
    if (!r.seek(kHdrVersionOff)) {
        return GameLodError::TooSmall;
    }
    if (!r.read_fixed_string(kHdrVersionSize, version_)) {
        return GameLodError::TooSmall;
    }
    // Accept any version beginning with "Game" (e.g. "GameMMVI"). Case-sensitive
    // match; the real files use exactly this casing.
    if (version_.size() < 4 || version_.compare(0, 4, "Game") != 0) {
        return GameLodError::UnsupportedVersion;
    }

    if (!r.seek(kHdrDescriptionOff)) {
        return GameLodError::TooSmall;
    }
    if (!r.read_fixed_string(kHdrDescriptionSize, description_)) {
        return GameLodError::TooSmall;
    }

    if (!r.seek(kHdrNumDirsOff)) {
        return GameLodError::TooSmall;
    }
    const std::uint32_t num_dirs = r.read_u32_le();
    if (num_dirs != 1) {
        return GameLodError::UnsupportedDirectoryCount;
    }

    // Root directory entry at offset 256.
    if (!r.seek(kRootEntryOffset)) {
        return GameLodError::TooSmall;
    }
    if (!r.read_fixed_string(kEntryNameSize, root_name_)) {
        return GameLodError::TooSmall;
    }
    if (!r.seek(kRootEntryOffset + kEntryDataOff)) {
        return GameLodError::TooSmall;
    }
    root_data_offset_ = r.read_u32_le();
    const std::uint32_t root_data_size = r.read_u32_le();
    if (!r.seek(kRootEntryOffset + kEntryNumItemsOff)) {
        return GameLodError::TooSmall;
    }
    num_items_ = r.read_u16_le();

    // Validate the root region: it must start after header+root and cover the
    // rest of the file. (Russian MM7 LODs are known to have a wrong dataSize;
    // we trust filesize like the reference reader does.)
    if (root_data_offset_ < kHeaderSize + kRootEntrySize || root_data_offset_ > data_.size()) {
        return GameLodError::BadRootOffset;
    }
    (void)root_data_size;  // not trusted; use filesize below

    // The file-entry table must fit within the root region.
    const std::uint64_t table_end = static_cast<std::uint64_t>(root_data_offset_) +
                                    static_cast<std::uint64_t>(num_items_) * kFileEntrySize;
    if (table_end > data_.size()) {
        return GameLodError::TableTooLarge;
    }

    entries_.clear();
    entries_.reserve(num_items_);
    for (std::uint16_t i = 0; i < num_items_; ++i) {
        const std::uint64_t entry_off = static_cast<std::uint64_t>(root_data_offset_) +
                                        static_cast<std::uint64_t>(i) * kFileEntrySize;
        if (!r.seek(static_cast<std::size_t>(entry_off))) {
            return GameLodError::TableTooLarge;
        }
        std::string name;
        if (!r.read_fixed_string(kEntryNameSize, name)) {
            return GameLodError::TableTooLarge;
        }
        if (!r.seek(static_cast<std::size_t>(entry_off + kEntryDataOff))) {
            return GameLodError::TableTooLarge;
        }
        const std::uint32_t rel_off = r.read_u32_le();
        const std::uint32_t size = r.read_u32_le();

        // File offsets are relative to the root directory's data offset.
        const std::uint64_t abs_off = static_cast<std::uint64_t>(root_data_offset_) + rel_off;
        const std::uint64_t abs_end = abs_off + size;
        if (abs_off > data_.size() || abs_end > data_.size()) {
            return GameLodError::EntryOutOfRange;
        }
        entries_.push_back(GameLodEntry{name, abs_off, size});
    }

    return GameLodError::None;
}

std::optional<GameLodEntry> GameLodArchive::find(std::string_view name) const {
    auto eq = [name](const GameLodEntry& e) {
        if (e.name.size() != name.size()) {
            return false;
        }
        return std::ranges::equal(e.name, name, [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    };
    auto it = std::ranges::find_if(entries_, eq);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return *it;
}

GameLodArchive::PayloadError GameLodArchive::payload(std::string_view name,
                                                     std::span<const std::byte>& out) const {
    auto entry = find(name);
    if (!entry) {
        return PayloadError::NotFound;
    }
    if (entry->data_offset + entry->data_size > data_.size()) {
        return PayloadError::OutOfRange;
    }
    out = std::span<const std::byte>(data_.data() + entry->data_offset, entry->data_size);
    return PayloadError::None;
}

}  // namespace starhaven::lod
