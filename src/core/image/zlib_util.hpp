#ifndef OPENMM6_CORE_IMAGE_ZLIB_UTIL_HPP
#define OPENMM6_CORE_IMAGE_ZLIB_UTIL_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openmm6::image::detail {

// zlib-inflate src into dst. Returns false on any zlib error. Uses auto-detect
// header mode (zlib or gzip). Shared by the bitmap and sprite decoders.
[[nodiscard]] bool inflate_all(std::span<const std::byte> src,
                               std::vector<std::uint8_t>& dst);

}  // namespace openmm6::image::detail

#endif  // OPENMM6_CORE_IMAGE_ZLIB_UTIL_HPP
