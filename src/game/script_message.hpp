#ifndef STARHAVEN_GAME_SCRIPT_MESSAGE_HPP
#define STARHAVEN_GAME_SCRIPT_MESSAGE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "game/script_walk.hpp"

namespace starhaven::game {

struct ScriptActorContext {
    std::size_t member = 0;
    int class_value = 0;
};

// An explicit script bank prevents a global continuation from accidentally
// entering a same-numbered map event. The map also owns global world effects.
struct ScriptContinuation {
    std::string map;
    std::uint16_t event = 0;
    int sequence = -1;
    bool global = false;
    bool npc_dialogue = false;
    WalkPresentation presentation;
    std::optional<std::uint32_t> decoration = std::nullopt;
    std::optional<ScriptActorContext> actor = std::nullopt;
};

class ScriptMessage {
public:
    void show(ScriptContinuation continuation, std::string text) {
        continuation_ = std::move(continuation);
        text_ = std::move(text);
        dismissed_ = false;
        scroll = 0;
    }

    [[nodiscard]] bool active() const noexcept { return continuation_.has_value(); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void dismiss() noexcept { dismissed_ = true; }
    void clear() { *this = {}; }

    // Consume exactly once, after the input batch. Keys queued behind the
    // dismissal still belong to the modal, including save and attack keys.
    [[nodiscard]] std::optional<ScriptContinuation> take_resume(std::string_view map) {
        if (!continuation_ || !dismissed_) {
            return std::nullopt;
        }
        auto result = std::move(*continuation_);
        clear();
        if (script_scope(result.map) != script_scope(map)) {
            return std::nullopt;
        }
        return result;
    }

    int scroll = 0;

private:
    std::optional<ScriptContinuation> continuation_;
    std::string text_;
    bool dismissed_ = false;
};

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_SCRIPT_MESSAGE_HPP
