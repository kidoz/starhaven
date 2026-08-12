#ifndef STARHAVEN_GAME_STARTUP_MEDIA_PLAYER_HPP
#define STARHAVEN_GAME_STARTUP_MEDIA_PLAYER_HPP

#include <cstdint>

namespace starhaven::assets {
class AssetCache;
}

namespace starhaven::game {

enum class OpeningMediaResult : std::uint8_t {
    ReachedTitle,
    Quit,
    PlatformError,
};

// SDL adapter for the two opening reels. It runs before any map, battle,
// clock, ambient source, or map-music object is constructed.
[[nodiscard]] OpeningMediaResult play_opening_media(assets::AssetCache& cache, int window_scale);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_STARTUP_MEDIA_PLAYER_HPP
