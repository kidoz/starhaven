#ifndef STARHAVEN_GAME_TEXT_HPP
#define STARHAVEN_GAME_TEXT_HPP

// Drawing the game's own bitmap fonts into the framebuffer. The font stores
// three pixel values — background, body, outline — and says nothing about what
// colours they are, so the caller chooses. See docs/formats/font.md.

#include <cstdint>
#include <string_view>

#include "core/image/font.hpp"
#include "core/render/color.hpp"
#include "core/render/rasterizer.hpp"

namespace starhaven::game {

// Draw `text` with its top-left corner at (x, y). Returns the pen's final x,
// so callers can chain runs. Pixels outside the framebuffer are dropped.
inline int draw_text(render::Framebuffer& fb, const image::Font& font, int x, int y,
                     std::string_view text, render::Color body, render::Color outline) {
    const auto pixels = fb.color();
    const int width = fb.width();
    const int height = fb.height();

    auto put = [&](int px, int py, render::Color c) {
        if (px < 0 || py < 0 || px >= width || py >= height) {
            return;
        }
        const auto i = (static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(px)) *
                       4;
        pixels[i] = c.r;
        pixels[i + 1] = c.g;
        pixels[i + 2] = c.b;
        pixels[i + 3] = 255;
    };

    int pen = x;
    for (const char ch : text) {
        const image::FontGlyph* g = font.glyph(static_cast<std::uint8_t>(ch));
        if (g == nullptr) {
            continue;
        }
        const int left = pen + g->left_spacing;
        for (int row = 0; row < font.height(); ++row) {
            for (int col = 0; col < g->width; ++col) {
                const std::uint8_t v =
                    g->pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(g->width) +
                              static_cast<std::size_t>(col)];
                if (v == image::kFontPixelBody) {
                    put(left + col, y + row, body);
                } else if (v == image::kFontPixelOutline) {
                    put(left + col, y + row, outline);
                }
            }
        }
        pen += g->advance();
    }
    return pen;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_TEXT_HPP
