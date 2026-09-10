#include "game/script_message_view.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

#include "game/text.hpp"

namespace starhaven::game {

void draw_script_message(render::Framebuffer& framebuffer, const image::Font& font,
                         ScriptMessage& message) {
    if (!message.active() || font.glyph_count() == 0) {
        return;
    }
    constexpr int kMargin = 24;
    constexpr int kPadding = 12;
    constexpr render::Color kPaper{36, 30, 24, 255};
    constexpr render::Color kText{245, 230, 185, 255};
    constexpr render::Color kOutline{0, 0, 0, 255};
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    const int line_height = std::max(1, font.height() + 2);
    const int text_width = std::max(1, width - 2 * (kMargin + kPadding));
    const int rows = std::max(1, (height - 2 * (kMargin + kPadding)) / line_height - 2);

    std::vector<std::string_view> lines;
    std::string_view remaining = message.text();
    while (!remaining.empty()) {
        std::size_t length = 0;
        std::size_t space = 0;
        while (length < remaining.size() && remaining[length] != '\n' &&
               font.text_width(remaining.substr(0, length + 1)) <= text_width) {
            if (remaining[length] == ' ') {
                space = length;
            }
            ++length;
        }
        if (length < remaining.size() && remaining[length] != '\n' && space > 0) {
            length = space;
        }
        if (length == 0 && remaining.front() != '\n') {
            length = 1;
        }
        lines.push_back(remaining.substr(0, length));
        remaining.remove_prefix(length);
        if (!remaining.empty() && (remaining.front() == '\n' || remaining.front() == ' ')) {
            remaining.remove_prefix(1);
        }
    }
    const int max_scroll = std::max(0, static_cast<int>(lines.size()) - rows);
    message.scroll = std::clamp(message.scroll, 0, max_scroll);
    const auto pixels = framebuffer.color();
    for (int y = kMargin; y < height - kMargin; ++y) {
        for (int x = kMargin; x < width - kMargin; ++x) {
            const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4;
            pixels[at] = kPaper.r;
            pixels[at + 1] = kPaper.g;
            pixels[at + 2] = kPaper.b;
            pixels[at + 3] = 255;
        }
    }
    for (int row = 0; row < rows && row + message.scroll < static_cast<int>(lines.size()); ++row) {
        draw_text(framebuffer, font, kMargin + kPadding, kMargin + kPadding + row * line_height,
                  lines[static_cast<std::size_t>(row) + static_cast<std::size_t>(message.scroll)],
                  kText, kOutline);
    }
    draw_text(framebuffer, font, kMargin + kPadding, height - kMargin - kPadding - line_height,
              max_scroll > 0 ? "Up/Down: scroll   Enter/Esc or click: continue"
                             : "Enter/Esc or click: continue",
              kText, kOutline);
}

}  // namespace starhaven::game
