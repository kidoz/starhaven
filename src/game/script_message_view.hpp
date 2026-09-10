#ifndef STARHAVEN_GAME_SCRIPT_MESSAGE_VIEW_HPP
#define STARHAVEN_GAME_SCRIPT_MESSAGE_VIEW_HPP

#include "core/image/font.hpp"
#include "core/render/rasterizer.hpp"
#include "game/script_message.hpp"

namespace starhaven::game {

void draw_script_message(render::Framebuffer& framebuffer, const image::Font& font,
                         ScriptMessage& message);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_MESSAGE_VIEW_HPP
