#include "core/random.hpp"

namespace starhaven {

std::uint16_t Mm6Random::next() noexcept {
    state_ = state_ * 0x343FDU + 0x269EC3U;
    return static_cast<std::uint16_t>((state_ >> 16U) & 0x7FFFU);
}

}  // namespace starhaven
