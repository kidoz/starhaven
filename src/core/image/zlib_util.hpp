#ifndef STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP
#define STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace starhaven::image::detail {

// zlib-inflate src into dst. Returns false on any zlib error. Uses auto-detect
// header mode (zlib or gzip). Shared by the bitmap and sprite decoders.
[[nodiscard]] bool inflate_all(std::span<const std::byte> src,
                               std::vector<std::uint8_t>& dst);

}  // namespace starhaven::image::detail

#endif  // STARHAVEN_CORE_IMAGE_ZLIB_UTIL_HPP
