#ifndef STARHAVEN_GAME_SCRIPT_OBJECT_EFFECTS_HPP
#define STARHAVEN_GAME_SCRIPT_OBJECT_EFFECTS_HPP

#include "game/launches.hpp"
#include "game/script_loot.hpp"
#include "game/temporary_objects.hpp"

namespace starhaven::game {

// Live opcode-34 application seam. Persistent loot owns the shared per-map
// random sequence. Temporary effects are session-local, like other launches:
// clear them only after successfully preparing a map (including save load).
class ScriptObjectEffects {
public:
    [[nodiscard]] LootSpawnResult spawn(const world::ObjectSpawnRequest& request,
                                        const world::MapSession& session,
                                        const data::ItemStatsTable& items, ScriptLootState& loot);
    [[nodiscard]] TemporaryObjectStep advance(double seconds, const world::MapSession& session);
    [[nodiscard]] std::vector<ActiveLaunch> sprites(const world::SpriteFrameTable& frames) const;
    [[nodiscard]] std::size_t active_count() const noexcept { return temporary_.active_count(); }
    void clear() noexcept;

private:
    TemporaryObjects temporary_;
    double tick_remainder_ = 0;
};

}  // namespace starhaven::game
#endif
