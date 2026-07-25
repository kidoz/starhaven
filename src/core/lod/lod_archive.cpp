#include "core/lod/lod_archive.hpp"

#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <ranges>

namespace starhaven::lod {

namespace {

// Verified layout constants (see docs/formats/lod.md).
constexpr std::uint32_t kHeaderSize = 288;  // 0x120
constexpr std::uint32_t kVersionOffset = 4;
constexpr std::uint32_t kVersionSize = 80;
constexpr std::uint32_t kLodTypeOffset = 0x100;  // 256
constexpr std::uint32_t kLodTypeSize = 16;
constexpr std::uint32_t kArchiveStartOffset = 0x110;  // 272
constexpr std::uint32_t kCountOffset = 0x11C;         // 284

constexpr std::uint32_t kEntrySize = 32;
constexpr std::uint32_t kEntryNameSize = 16;
constexpr std::uint32_t kEntryAddrOffset = 16;
constexpr std::uint32_t kEntrySizeFieldOffset = 20;
constexpr std::uint32_t kEntryUnpackedOffset = 24;

// Heroes III uses a version code <= 0xFFFF at offset 4; we only support MM6+.
constexpr std::uint32_t kVersionCodeHeroes3Max = 0xFFFF;

[[nodiscard]] LodKind kind_from_lod_type(const std::string& lod_type) {
    // lod_type is case-mixed in the real files ("bitmaps", "sprites08",
    // "icons", "game"). Match case-insensitively and tolerate a numeric suffix
    // such as the "08" seen on SPRITES.LOD.
    std::string lower;
    lower.reserve(lod_type.size());
    std::ranges::transform(lod_type, std::back_inserter(lower),
                           [](unsigned char c) { return std::tolower(c); });

    auto starts_with = [&lower](const char* prefix) { return lower.rfind(prefix, 0) == 0; };
    if (starts_with("bitmaps"))
        return LodKind::Bitmaps;
    if (starts_with("sprites"))
        return LodKind::Sprites;
    if (starts_with("icons"))
        return LodKind::Icons;
    if (starts_with("game"))
        return LodKind::Game;
    return LodKind::Unknown;
}

// Read the whole file into memory. MM6 archives are tens of megabytes, which is
// acceptable for a loader/browser; a streaming variant can come later.
[[nodiscard]] LodError read_file(const std::filesystem::path& path, std::vector<std::byte>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return LodError::Io;
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return LodError::Io;
    }
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (!f) {
            return LodError::Io;
        }
    }
    return LodError::None;
}

}  // namespace

LodError LodArchive::open(const std::filesystem::path& path, LodArchive& out) {
    std::vector<std::byte> data;
    if (LodError e = read_file(path, data); e != LodError::None) {
        return e;
    }
    return LodArchive::parse(std::move(data), out);
}

LodError LodArchive::parse(std::vector<std::byte> data, LodArchive& out) {
    out = LodArchive{};
    out.data_ = std::move(data);
    return out.parse_impl();
}

LodError LodArchive::parse_impl() {
    if (data_.size() < kHeaderSize) {
        return LodError::TooSmall;
    }

    io::ByteReader r{std::span<const std::byte>{data_}};

    // Magic "LOD\0".
    if (r.read_u8() != 'L' || r.read_u8() != 'O' || r.read_u8() != 'D' || r.read_u8() != 0) {
        return LodError::BadMagic;
    }

    // Version code at offset 4. The first u32 of the version string
    // discriminates Heroes III (<= 0xFFFF) from MM6+.
    const std::uint32_t version_code = r.read_u32_le();
    if (version_code <= kVersionCodeHeroes3Max) {
        return LodError::UnsupportedVersion;
    }

    // Read the 80-byte version string starting at offset 4 (we already consumed
    // 8 bytes; back up to read it as a fixed field).
    if (!r.seek(kVersionOffset)) {
        return LodError::TooSmall;
    }
    if (!r.read_fixed_string(kVersionSize, version_)) {
        return LodError::TooSmall;
    }
    if (version_ != "MMVI") {
        // Only MM6 ("MMVI") is in scope for this engine.
        return LodError::UnknownVersion;
    }

    // lod_type at offset 0x100.
    if (!r.seek(kLodTypeOffset)) {
        return LodError::TooSmall;
    }
    if (!r.read_fixed_string(kLodTypeSize, lod_type_)) {
        return LodError::TooSmall;
    }
    kind_ = kind_from_lod_type(lod_type_);
    if (kind_ == LodKind::Unknown) {
        return LodError::UnknownLodType;
    }

    // archive_start (u32 at 0x110) and count (u16 at 0x11C).
    if (!r.seek(kArchiveStartOffset)) {
        return LodError::TooSmall;
    }
    archive_start_ = r.read_u32_le();
    if (!r.seek(kCountOffset)) {
        return LodError::TooSmall;
    }
    count_ = r.read_u16_le();

    // Games.lod uses a different directory layout and is not parsed here.
    if (kind_ == LodKind::Game) {
        // Enumerate is still unsupported for this variant; surface as a clear
        // error so callers know it is intentionally deferred.
        return LodError::UnknownLodType;
    }

    // Validate archive_start and that the directory table fits.
    if (archive_start_ < kHeaderSize || archive_start_ > data_.size()) {
        return LodError::BadArchiveStart;
    }
    const std::uint64_t dir_bytes = static_cast<std::uint64_t>(count_) * kEntrySize;
    const std::uint64_t dir_end = static_cast<std::uint64_t>(archive_start_) + dir_bytes;
    if (dir_end > data_.size()) {
        return LodError::CountTooLarge;
    }

    // Read raw addresses first; sizes come from gaps (verified layout for
    // bitmaps/icons; size_field is a fallback for sprites).
    struct Raw {
        std::string name;
        std::uint32_t addr;  // relative to archive_start
        std::uint32_t size_field;
        std::uint32_t unpacked;
    };
    std::vector<Raw> raws;
    raws.reserve(count_);

    for (std::uint16_t i = 0; i < count_; ++i) {
        const std::uint64_t entry_off =
            static_cast<std::uint64_t>(archive_start_) + static_cast<std::uint64_t>(i) * kEntrySize;
        if (!r.seek(static_cast<std::size_t>(entry_off))) {
            return LodError::CountTooLarge;
        }
        std::string name;
        if (!r.read_fixed_string(kEntryNameSize, name)) {
            return LodError::CountTooLarge;
        }
        if (!r.seek(static_cast<std::size_t>(entry_off + kEntryAddrOffset))) {
            return LodError::CountTooLarge;
        }
        const std::uint32_t addr = r.read_u32_le();
        const std::uint32_t size_field = r.read_u32_le();
        const std::uint32_t unpacked = r.read_u32_le();
        raws.push_back(Raw{name, addr, size_field, unpacked});
    }

    // Resolve absolute offsets and sizes. In the real MM6 archives the entry
    // table is NOT sorted by data address and entries are not packed in table
    // order, so the gap-to-next-entry heuristic is unreliable. The size_field
    // at entry+0x14 is the authoritative stored size (verified on BITMAPS.LOD:
    // every entry's archive_start+addr+size_field stays in bounds and the
    // maximum end equals the file size exactly).
    entries_.clear();
    entries_.reserve(raws.size());
    for (const auto& cur : raws) {
        const std::uint64_t abs_addr = static_cast<std::uint64_t>(archive_start_) + cur.addr;
        if (abs_addr > data_.size()) {
            return LodError::EntryOffsetOutOfRange;
        }

        const std::uint64_t end = abs_addr + cur.size_field;
        if (end > data_.size()) {
            return LodError::EntrySizeOutOfRange;
        }

        entries_.push_back(LodEntry{
            cur.name,
            abs_addr,
            cur.size_field,
            cur.unpacked,
            /*uncompressed=*/cur.unpacked == 0,
        });
    }

    return LodError::None;
}

std::optional<LodEntry> LodArchive::find(std::string_view name) const {
    auto eq = [name](const LodEntry& e) {
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

LodArchive::PayloadError LodArchive::payload(std::string_view name,
                                             std::span<const std::byte>& out) const {
    auto entry = find(name);
    if (!entry) {
        return PayloadError::NotFound;
    }
    if (!entry->uncompressed) {
        // Compressed payload decoding is a follow-up slice.
        return PayloadError::Compressed;
    }
    if (entry->data_offset + entry->stored_size > data_.size()) {
        return PayloadError::OutOfRange;
    }
    out = std::span<const std::byte>(data_.data() + entry->data_offset, entry->stored_size);
    return PayloadError::None;
}

}  // namespace starhaven::lod
