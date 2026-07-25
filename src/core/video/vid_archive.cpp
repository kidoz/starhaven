#include "core/video/vid_archive.hpp"

#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace starhaven::video {

namespace {

// A directory of more entries than this is treated as a malformed file rather
// than allocated: MM6's larger archive has 87.
constexpr std::uint32_t kMaxEntries = 65536;

[[nodiscard]] char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return lower(x) == lower(y); });
}

}  // namespace

VidError VidArchive::read_directory(std::span<const std::byte> header,
                                    std::uint64_t file_size,
                                    std::vector<VidEntry>& out) {
    out.clear();

    io::ByteReader r{header};
    if (header.size() < 4) {
        return VidError::TooSmall;
    }
    const std::uint32_t count = r.read_u32_le();
    if (count == 0 || count > kMaxEntries) {
        return VidError::BadCount;
    }

    // The directory must fit in the file, and so must the data it points at.
    const std::uint64_t dir_end =
        4 + static_cast<std::uint64_t>(count) * kVidEntrySize;
    if (dir_end > file_size || dir_end > header.size()) {
        return VidError::BadCount;
    }

    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!r.seek(4 + static_cast<std::size_t>(i) * kVidEntrySize)) {
            return VidError::BadCount;
        }
        VidEntry e;
        if (!r.read_fixed_string(kVidNameSize, e.name)) {
            return VidError::BadCount;
        }
        e.offset = r.read_u32_le();
        if (!r.ok()) {
            return VidError::BadCount;
        }
        // Offsets must land after the directory and inside the file.
        if (e.offset < dir_end || e.offset > file_size) {
            return VidError::BadOffset;
        }
        out.push_back(std::move(e));
    }

    // Sizes come from the gap to the next entry; the last runs to the file end.
    // Ascending offsets are what makes that derivation sound, so require them.
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint64_t end =
            (i + 1 < out.size()) ? out[i + 1].offset : file_size;
        if (end < out[i].offset) {
            return VidError::BadOffset;
        }
        out[i].size = end - out[i].offset;
    }
    return VidError::None;
}

VidError VidArchive::open(const std::filesystem::path& path, VidArchive& out) {
    out = VidArchive{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return VidError::Io;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end < 0) {
        return VidError::Io;
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size < 4) {
        return VidError::TooSmall;
    }

    // Read the count first, then exactly the directory it declares.
    file.seekg(0, std::ios::beg);
    std::array<char, 4> count_bytes{};
    if (!file.read(count_bytes.data(), 4)) {
        return VidError::Io;
    }
    const auto count =
        static_cast<std::uint32_t>(static_cast<unsigned char>(count_bytes[0])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(count_bytes[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(count_bytes[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(count_bytes[3])) << 24);
    if (count == 0 || count > kMaxEntries) {
        return VidError::BadCount;
    }
    const std::uint64_t dir_end =
        4 + static_cast<std::uint64_t>(count) * kVidEntrySize;
    if (dir_end > file_size) {
        return VidError::BadCount;
    }

    std::vector<std::byte> header(static_cast<std::size_t>(dir_end));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(header.data()),
                   static_cast<std::streamsize>(header.size()))) {
        return VidError::Io;
    }

    std::vector<VidEntry> entries;
    if (const VidError e = read_directory(header, file_size, entries);
        e != VidError::None) {
        return e;
    }

    out.path_ = path;
    out.file_ = std::move(file);
    out.file_size_ = file_size;
    out.entries_ = std::move(entries);
    out.in_memory_ = false;
    return VidError::None;
}

VidError VidArchive::parse(std::vector<std::byte> data, VidArchive& out) {
    out = VidArchive{};

    std::vector<VidEntry> entries;
    if (const VidError e = read_directory(data, data.size(), entries);
        e != VidError::None) {
        return e;
    }
    out.memory_ = std::move(data);
    out.file_size_ = out.memory_.size();
    out.entries_ = std::move(entries);
    out.in_memory_ = true;
    return VidError::None;
}

std::size_t VidArchive::find(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (iequals(entries_[i].name, name)) {
            return i;
        }
    }
    return entries_.size();
}

bool VidArchive::read(std::size_t index, std::vector<std::byte>& out) {
    out.clear();
    if (index >= entries_.size()) {
        return false;
    }
    const VidEntry& e = entries_[index];
    if (e.offset + e.size > file_size_) {
        return false;
    }
    const auto size = static_cast<std::size_t>(e.size);

    if (in_memory_) {
        const auto begin = memory_.begin() + static_cast<std::ptrdiff_t>(e.offset);
        out.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
        return true;
    }

    if (!file_.is_open()) {
        return false;
    }
    out.resize(size);
    file_.clear();  // a previous read may have hit EOF
    file_.seekg(static_cast<std::streamoff>(e.offset), std::ios::beg);
    if (!file_.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(size))) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace starhaven::video
