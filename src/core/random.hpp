#ifndef STARHAVEN_CORE_RANDOM_HPP
#define STARHAVEN_CORE_RANDOM_HPP

#include <cstdint>

namespace starhaven {

// The process-wide generator used by MM6, represented as an explicit value so
// callers can reproduce a sequence without hidden global state. The original
// executable seeds this state once from the Windows millisecond tick count.
class Mm6Random {
public:
    explicit constexpr Mm6Random(std::uint32_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint16_t next() noexcept;
    [[nodiscard]] constexpr std::uint32_t state() const noexcept { return state_; }

private:
    std::uint32_t state_;
};

}  // namespace starhaven

#endif  // STARHAVEN_CORE_RANDOM_HPP
