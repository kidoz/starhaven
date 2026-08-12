#ifndef STARHAVEN_GAME_INSTALL_PROMPT_HPP
#define STARHAVEN_GAME_INSTALL_PROMPT_HPP

#include <optional>

#include "core/platform/game_install.hpp"

namespace starhaven::game {

// Present the first-run installation recovery screen. The selected MM6
// installation is validated read-only before this function returns it.
[[nodiscard]] std::optional<platform::GameInstall>
prompt_for_game_install(const platform::InstallValidation& initial_validation);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INSTALL_PROMPT_HPP
