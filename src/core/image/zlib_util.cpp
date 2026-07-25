#include "core/image/zlib_util.hpp"

#include <array>

#include <zlib.h>

namespace starhaven::image::detail {

bool inflate_all(std::span<const std::byte> src,
                 std::vector<std::uint8_t>& dst) {
    dst.clear();

    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    // windowBits = 15 + 32 asks zlib to auto-detect zlib or gzip headers.
    if (inflateInit2(&zs, 15 + 32) != Z_OK) {
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

}  // namespace starhaven::image::detail
