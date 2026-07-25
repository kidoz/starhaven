#ifndef STARHAVEN_CORE_IO_BYTE_READER_HPP
#define STARHAVEN_CORE_IO_BYTE_READER_HPP

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace starhaven::io {

// Error reasons reported by the reader. Callers decide how to surface them;
// the reader never throws and never reads past its span.
enum class ReadError {
    None,
    // The requested field would extend beyond the end of the buffer.
    OutOfBounds,
    // A counted read (string, array) declared a length the buffer cannot hold.
    CountTooLarge,
    // A string was not NUL-terminated inside the available space.
    UnterminatedString,
};

// A bounds-checked, little-endian reader over a non-owning byte span.
//
// The Might and Magic resource formats are byte-oriented and untrusted, so
// this reader is the single chokepoint that turns malformed, truncated, or
// hostile input into deterministic ReadError values instead of undefined
// behavior. It never maps raw bytes onto C++ structs.
class ByteReader {
public:
    ByteReader() noexcept = default;
    explicit ByteReader(std::span<const std::byte> data) noexcept : data_(data) {}

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] bool eof() const noexcept { return pos_ >= data_.size(); }
    [[nodiscard]] ReadError last_error() const noexcept { return error_; }
    [[nodiscard]] bool ok() const noexcept { return error_ == ReadError::None; }

    void reset(std::span<const std::byte> data) noexcept {
        data_ = data;
        pos_ = 0;
        error_ = ReadError::None;
    }

    // Seek to an absolute offset. Returns false (and sets OutOfBounds) if the
    // offset is outside [0, size()]. Seeking to exactly size() is allowed and
    // represents end-of-stream.
    [[nodiscard]] bool seek(std::size_t offset) noexcept;

    // Skip forward by n bytes. Returns false on overflow past the end.
    [[nodiscard]] bool skip(std::size_t n) noexcept;

    // Checked primitive readers. On failure they set last_error(), leave the
    // cursor unchanged, and return a zero value.
    [[nodiscard]] std::uint8_t  read_u8() noexcept;
    [[nodiscard]] std::uint16_t read_u16_le() noexcept;
    [[nodiscard]] std::uint32_t read_u32_le() noexcept;
    [[nodiscard]] std::int32_t  read_i32_le() noexcept;

    // Read exactly n bytes into out. Returns false if out is smaller than n or
    // if the cursor would overflow.
    [[nodiscard]] bool read_bytes(std::span<std::byte> out) noexcept;

    // Read a fixed-width ASCII field (e.g. a magic or a name[N] slot) and copy
    // the bytes up to the first NUL or the field width into out. The trailing
    // NUL is not copied. Returns false if the field would overflow the buffer.
    [[nodiscard]] bool read_fixed_string(std::size_t field_width,
                                         std::string& out) noexcept;

private:
    // Inspect a region without advancing; returns false on overflow.
    [[nodiscard]] bool peek(std::size_t n, std::span<const std::byte>& out) const noexcept;

    std::span<const std::byte> data_{};
    std::size_t pos_ = 0;
    ReadError error_ = ReadError::None;
};

}  // namespace starhaven::io

#endif  // STARHAVEN_CORE_IO_BYTE_READER_HPP
