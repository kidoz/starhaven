#ifndef STARHAVEN_CORE_IMAGE_PCX_HPP
#define STARHAVEN_CORE_IMAGE_PCX_HPP

// The interface panels that ship as PCX: `Border1.pcx`, `Border2.pcx` and
// kin in `icons.lod`. The entry wears the same 48-byte container as every
// other image there, but its payload inflates to a PCX file — magic 0x0A,
// RLE-packed rows — rather than raw palette indices. `observed` The two
// shapes the shipped files use are 3-plane 24-bit (the borders) and
// single-plane 8-bit with a 768-byte palette at the tail.

#include <cstddef>
#include <span>

#include "core/image/bitmap.hpp"

namespace starhaven::image {

// Decode a `.LOD` entry whose payload is a PCX. Reuses BitmapError for the
// failure vocabulary: TooSmall for a missing container or PCX header,
// InflateFailed for zlib, BadDimensions for a shape this reader does not
// know (only 8-bit paletted and 24-bit 3-plane appear in the shipped data).
[[nodiscard]] BitmapError decode_pcx_entry(std::span<const std::byte> entry, Bitmap& out);

}  // namespace starhaven::image

#endif  // STARHAVEN_CORE_IMAGE_PCX_HPP
