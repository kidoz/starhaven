#include "core/image/zlib_util.hpp"

#include <array>

#include <zlib.h>

namespace starhaven::image::detail {

namespace {

// Shared inflate loop. `window_bits` selects the header mode.
[[nodiscard]] bool inflate_with(std::span<const std::byte> src, std::vector<std::uint8_t>& dst,
                                int window_bits) {
    dst.clear();

    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    if (inflateInit2(&zs, window_bits) != Z_OK) {
        return false;
    }

    constexpr std::size_t kChunk = 16 * 1024;
    std::array<std::uint8_t, kChunk> buf{};
    bool ok = true;
    for (;;) {
        zs.next_out = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        const int rc = ::inflate(&zs, Z_NO_FLUSH);
        const std::size_t produced = buf.size() - zs.avail_out;
        dst.insert(dst.end(), buf.data(), buf.data() + produced);
        if (rc == Z_STREAM_END) {
            break;
        }
        if (rc != Z_OK) {
            ok = false;
            break;
        }
        if (produced == 0 && zs.avail_in == 0) {
            // No progress possible; treat as malformed input.
            ok = false;
            break;
        }
    }
    inflateEnd(&zs);
    return ok;
}

}  // namespace

bool inflate_all(std::span<const std::byte> src, std::vector<std::uint8_t>& dst) {
    // windowBits = 15 + 32 asks zlib to auto-detect zlib or gzip headers.
    return inflate_with(src, dst, 15 + 32);
}

bool inflate_raw(std::span<const std::byte> src, std::vector<std::uint8_t>& dst) {
    // Raw deflate has no header to skip past and no checksum to verify, so
    // drop the stream's own 2-byte zlib header first.
    dst.clear();
    if (src.size() < 2) {
        return false;
    }
    return inflate_with(src.subspan(2), dst, -15);
}

}  // namespace starhaven::image::detail
