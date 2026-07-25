#ifndef OPENMM6_CORE_RENDER_COLOR_HPP
#define OPENMM6_CORE_RENDER_COLOR_HPP

#include <cstdint>

namespace openmm6::render {

// An RGBA color (8-bit channels).
//
// Lives in its own header so that both the rasterizer and the texture sampler
// can use it without either depending on the other.
struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

}  // namespace openmm6::render

#endif  // OPENMM6_CORE_RENDER_COLOR_HPP
