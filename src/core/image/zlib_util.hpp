#ifndef STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP
#define STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace starhaven::image::detail {

// zlib-inflate src into dst. Returns false on any zlib error. Uses auto-detect
// header mode (zlib or gzip). Shared by the bitmap and sprite decoders.
[[nodiscard]] bool inflate_all(std::span<const std::byte> src, std::vector<std::uint8_t>& dst);

// Inflate a zlib stream as raw deflate, skipping its 2-byte header and
// ignoring the trailing Adler-32 checksum.
//
// Some shipped game data carries a corrupt checksum over intact deflate data;
// the original engine never verified it. Callers must confirm the result some
// other way — normally by checking the produced length against a declared
// size — because this cannot distinguish good data from damaged data.
[[nodiscard]] bool inflate_raw(std::span<const std::byte> src, std::vector<std::uint8_t>& dst);

}  // namespace starhaven::image::detail

#endif  // STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP
