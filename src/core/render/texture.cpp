#include "core/render/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace starhaven::render {

bool Texture::create(int width, int height, std::vector<std::uint8_t> rgba,
                     Texture& out) {
    out = Texture{};
    if (width <= 0 || height <= 0) {
        return false;
    }
    // Compute the expected size in a width type that cannot overflow for any
    // int dimensions, so a hostile archive cannot wrap the product and pass a
    // short buffer as valid.
    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4U;
    if (rgba.size() != expected) {
        return false;
    }
    out.width_ = width;
    out.height_ = height;
    out.rgba_ = std::move(rgba);
    return true;
}

Color Texture::texel(int x, int y, WrapMode wrap) const noexcept {
    if (rgba_.empty()) {
        return Color{0, 0, 0, 0};
    }
    if (wrap == WrapMode::Repeat) {
        // C++ % keeps the sign of the dividend, so a negative coordinate needs
        // the extra bias to land in [0, extent).
        x %= width_;
        if (x < 0) {
            x += width_;
        }
        y %= height_;
        if (y < 0) {
            y += height_;
        }
    } else {
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
    }
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) *
        4U;
    return Color{rgba_[idx], rgba_[idx + 1], rgba_[idx + 2], rgba_[idx + 3]};
}

Color Texture::sample(float u, float v, WrapMode wrap) const noexcept {
    if (rgba_.empty()) {
        return Color{0, 0, 0, 0};
    }
    // A degenerate projection can produce inf/NaN here. Resolve to the first
    // texel instead of letting the float->int conversion become undefined.
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return texel(0, 0, wrap);
    }

    // Reduce to a bounded range *before* scaling by the extent. Scaling first
    // would let a large coordinate overflow int on conversion.
    if (wrap == WrapMode::Repeat) {
        u -= std::floor(u);  // [0,1)
        v -= std::floor(v);
    } else {
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);
    }

    int x = static_cast<int>(u * static_cast<float>(width_));
    int y = static_cast<int>(v * static_cast<float>(height_));
    // u can round to exactly 1.0 after the subtraction above, and the Clamp
    // path admits 1.0 by construction; both would index one past the edge.
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);

    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x)) *
        4U;
    return Color{rgba_[idx], rgba_[idx + 1], rgba_[idx + 2], rgba_[idx + 3]};
}

}  // namespace starhaven::render
