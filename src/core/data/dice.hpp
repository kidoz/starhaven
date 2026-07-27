#ifndef STARHAVEN_CORE_DATA_DICE_HPP
#define STARHAVEN_CORE_DATA_DICE_HPP

// The damage notation the design tables write: "1d6", "3D3", "2d6+2".
//
// Both `MONSTERS.TXT`'s two attack columns and `ITEMS.TXT`'s weapon modifier
// use it, in one of exactly two forms; see docs/formats/text-tables.md.

#include <cstdint>
#include <string_view>

#include "core/random.hpp"

namespace starhaven::data {

// A parsed roll. `count` dice of `sides` faces, plus a flat bonus.
struct Dice {
    int count = 0;
    int sides = 0;
    int bonus = 0;

    [[nodiscard]] bool empty() const noexcept { return count <= 0 || sides <= 0; }

    // The smallest and largest this can come to, for tests and for showing a
    // weapon's range without rolling it.
    [[nodiscard]] int lowest() const noexcept { return empty() ? 0 : count + bonus; }
    [[nodiscard]] int highest() const noexcept { return empty() ? 0 : count * sides + bonus; }
};

// Parse one cell. Returns an empty roll for anything that is not the notation,
// including the literal "0" the tables use for "no attack" — 212 of the 346
// monster attack cells say exactly that.
[[nodiscard]] Dice parse_dice(std::string_view text) noexcept;

// Roll it. Each die is drawn separately, so 2d6 is not 1d11+1.
[[nodiscard]] int roll(const Dice& dice, Mm6Random& random) noexcept;

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_DICE_HPP
