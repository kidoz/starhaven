#include "core/data/dice.hpp"

#include <cctype>
#include <cstddef>

#include "core/data/text_table.hpp"

namespace starhaven::data {

namespace {

// Read digits at `at`, advancing it. Returns -1 when there are none.
int read_number(std::string_view text, std::size_t& at) noexcept {
    std::size_t start = at;
    int value = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
        value = value * 10 + (text[at] - '0');
        ++at;
    }
    return at == start ? -1 : value;
}

}  // namespace

Dice parse_dice(std::string_view text) noexcept {
    const std::string_view trimmed = trim(text);
    std::size_t at = 0;
    const int count = read_number(trimmed, at);
    if (count <= 0 || at >= trimmed.size()) {
        return {};
    }
    // The tables spell it both ways: "3d3" and "1D6+1".
    if (trimmed[at] != 'd' && trimmed[at] != 'D') {
        return {};
    }
    ++at;
    const int sides = read_number(trimmed, at);
    if (sides <= 0) {
        return {};
    }
    int bonus = 0;
    if (at < trimmed.size() && (trimmed[at] == '+' || trimmed[at] == '-')) {
        const bool negative = trimmed[at] == '-';
        ++at;
        const int magnitude = read_number(trimmed, at);
        if (magnitude < 0) {
            return {};
        }
        bonus = negative ? -magnitude : magnitude;
    }
    // Trailing anything means this is not the notation, whatever it is.
    if (at != trimmed.size()) {
        return {};
    }
    return {count, sides, bonus};
}

int roll(const Dice& dice, Mm6Random& random) noexcept {
    if (dice.empty()) {
        return 0;
    }
    int total = dice.bonus;
    for (int i = 0; i < dice.count; ++i) {
        total += 1 + static_cast<int>(random.next() % static_cast<unsigned>(dice.sides));
    }
    return total;
}

}  // namespace starhaven::data
