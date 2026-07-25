#include "core/audio/snd_archive.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace starhaven::audio {

using image::detail::inflate_all;

namespace {

// A directory larger than this is treated as malformed rather than allocated:
// MM6's archive holds 1,526 entries.
constexpr std::uint32_t kMaxEntries = 1u << 20;

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

SndError SndArchive::read_directory(std::span<const std::byte> header,
                                    std::uint64_t file_size,
                                    std::vector<SndEntry>& out) {
    out.clear();
    if (header.size() < 4) {
        return SndError::TooSmall;
    }

    io::ByteReader r{header};
    const std::uint32_t count = r.read_u32_le();
    if (count == 0 || count > kMaxEntries) {
        return SndError::BadCount;
    }
    const std::uint64_t dir_end =
        4 + static_cast<std::uint64_t>(count) * kSndEntrySize;
    if (dir_end > file_size || dir_end > header.size()) {
        return SndError::BadCount;
    }

    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!r.seek(4 + static_cast<std::size_t>(i) * kSndEntrySize)) {
            return SndError::BadCount;
        }
        SndEntry e;
        if (!r.read_fixed_string(kSndNameSize, e.name)) {
            return SndError::BadCount;
        }
        e.offset = r.read_u32_le();
        e.packed_size = r.read_u32_le();
        e.unpacked_size = r.read_u32_le();
        if (!r.ok()) {
            return SndError::BadCount;
        }
        if (e.offset < dir_end || e.offset + e.packed_size > file_size) {
            return SndError::BadOffset;
        }
        out.push_back(std::move(e));
    }
    return SndError::None;
}

SndError SndArchive::open(const std::filesystem::path& path, SndArchive& out) {
    out = SndArchive{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return SndError::Io;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end < 0) {
        return SndError::Io;
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size < 4) {
        return SndError::TooSmall;
    }

    file.seekg(0, std::ios::beg);
    std::array<char, 4> count_bytes{};
    if (!file.read(count_bytes.data(), 4)) {
        return SndError::Io;
    }
    std::uint32_t count = 0;
    for (int i = 0; i < 4; ++i) {
        count |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(count_bytes[static_cast<std::size_t>(i)]))
                 << (8 * i);
    }
    if (count == 0 || count > kMaxEntries) {
        return SndError::BadCount;
    }
    const std::uint64_t dir_end =
        4 + static_cast<std::uint64_t>(count) * kSndEntrySize;
    if (dir_end > file_size) {
        return SndError::BadCount;
    }

    std::vector<std::byte> header(static_cast<std::size_t>(dir_end));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(header.data()),
                   static_cast<std::streamsize>(header.size()))) {
        return SndError::Io;
    }

    std::vector<SndEntry> entries;
    if (const SndError e = read_directory(header, file_size, entries);
        e != SndError::None) {
        return e;
    }

    out.path_ = path;
    out.file_ = std::move(file);
    out.file_size_ = file_size;
    out.entries_ = std::move(entries);
    out.in_memory_ = false;
    return SndError::None;
}

SndError SndArchive::parse(std::vector<std::byte> data, SndArchive& out) {
    out = SndArchive{};
    std::vector<SndEntry> entries;
    if (const SndError e = read_directory(data, data.size(), entries);
        e != SndError::None) {
        return e;
    }
    out.memory_ = std::move(data);
    out.file_size_ = out.memory_.size();
    out.entries_ = std::move(entries);
    out.in_memory_ = true;
    return SndError::None;
}

std::size_t SndArchive::find(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (iequals(entries_[i].name, name)) {
            return i;
        }
    }
    return entries_.size();
}

SndError SndArchive::read(std::size_t index, std::vector<std::uint8_t>& out) {
    out.clear();
    if (index >= entries_.size()) {
        return SndError::BadOffset;
    }
    const SndEntry& e = entries_[index];

    std::vector<std::byte> stored(e.packed_size);
    if (in_memory_) {
        if (e.offset + e.packed_size > memory_.size()) {
            return SndError::BadOffset;
        }
        std::copy_n(memory_.begin() + static_cast<std::ptrdiff_t>(e.offset),
                    e.packed_size, stored.begin());
    } else {
        if (!file_.is_open()) {
            return SndError::Io;
        }
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(e.offset), std::ios::beg);
        if (!file_.read(reinterpret_cast<char*>(stored.data()),
                        static_cast<std::streamsize>(stored.size()))) {
            return SndError::Io;
        }
    }

    // Matching sizes mean the entry was never compressed. One of MM6's 1,526
    // is stored this way, and treating it as zlib fails outright.
    if (e.stored()) {
        out.resize(stored.size());
        std::transform(stored.begin(), stored.end(), out.begin(),
                       [](std::byte b) { return static_cast<std::uint8_t>(b); });
        return SndError::None;
    }

    if (!inflate_all(stored, out)) {
        return SndError::InflateFailed;
    }
    return SndError::None;
}

}  // namespace starhaven::audio
