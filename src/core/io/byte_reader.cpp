#include "core/io/byte_reader.hpp"

#include <cstring>

namespace openmm6::io {

namespace {

// A local, checked helper: adds a and b, capping at SIZE_MAX instead of
// wrapping. Used to make "offset + length" computations safe against
// overflow from hostile counts.
[[nodiscard]] bool checked_add(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (b > SIZE_MAX - a) {
        return false;
    }
    out = a + b;
    return true;
}

}  // namespace

bool ByteReader::peek(std::size_t n, std::span<const std::byte>& out) const noexcept {
    std::size_t end = 0;
    if (!checked_add(pos_, n, end)) {
        return false;
    }
    if (end > data_.size()) {
        return false;
    }
    out = data_.subspan(pos_, n);
    return true;
}

bool ByteReader::seek(std::size_t offset) noexcept {
    if (offset > data_.size()) {
        error_ = ReadError::OutOfBounds;
        return false;
    }
    pos_ = offset;
    error_ = ReadError::None;
    return true;
}

bool ByteReader::skip(std::size_t n) noexcept {
    std::size_t next = 0;
    if (!checked_add(pos_, n, next) || next > data_.size()) {
        error_ = ReadError::OutOfBounds;
        return false;
    }
    pos_ = next;
    return true;
}

std::uint8_t ByteReader::read_u8() noexcept {
    std::span<const std::byte> span;
    if (!peek(1, span)) {
        error_ = ReadError::OutOfBounds;
        return 0;
    }
    ++pos_;
    return static_cast<std::uint8_t>(span[0]);
}

std::uint16_t ByteReader::read_u16_le() noexcept {
    std::span<const std::byte> span;
    if (!peek(2, span)) {
        error_ = ReadError::OutOfBounds;
        return 0;
    }
    pos_ += 2;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(span[0]) |
        (static_cast<std::uint16_t>(span[1]) << 8));
}

std::uint32_t ByteReader::read_u32_le() noexcept {
    std::span<const std::byte> span;
    if (!peek(4, span)) {
        error_ = ReadError::OutOfBounds;
        return 0;
    }
    pos_ += 4;
    return static_cast<std::uint32_t>(span[0]) |
           (static_cast<std::uint32_t>(span[1]) << 8) |
           (static_cast<std::uint32_t>(span[2]) << 16) |
           (static_cast<std::uint32_t>(span[3]) << 24);
}

std::int32_t ByteReader::read_i32_le() noexcept {
    return static_cast<std::int32_t>(read_u32_le());
}

bool ByteReader::read_bytes(std::span<std::byte> out) noexcept {
    std::span<const std::byte> span;
    if (!peek(out.size(), span)) {
        error_ = (out.size() > data_.size()) ? ReadError::CountTooLarge
                                             : ReadError::OutOfBounds;
        return false;
    }
    if (!out.empty()) {
        std::memcpy(out.data(), span.data(), out.size());
    }
    pos_ += out.size();
    return true;
}

bool ByteReader::read_fixed_string(std::size_t field_width, std::string& out) noexcept {
    std::span<const std::byte> span;
    if (!peek(field_width, span)) {
        error_ = (field_width > data_.size()) ? ReadError::CountTooLarge
                                              : ReadError::OutOfBounds;
        return false;
    }
    out.clear();
    out.reserve(field_width);
    for (std::size_t i = 0; i < field_width; ++i) {
        const auto ch = static_cast<char>(span[i]);
        if (ch == '\0') {
            break;
        }
        out.push_back(ch);
    }
    pos_ += field_width;
    return true;
}

}  // namespace openmm6::io
