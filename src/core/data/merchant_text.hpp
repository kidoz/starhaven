#ifndef STARHAVEN_CORE_DATA_MERCHANT_TEXT_HPP
#define STARHAVEN_CORE_DATA_MERCHANT_TEXT_HPP

// `Merchant.txt`: what a shopkeeper says, by what you are trying to do and how
// good you are at haggling. Six situations by four actions.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/data/text_table.hpp"

namespace starhaven::data {

// The four things you can do at a counter, in the table's column order.
enum class MerchantAction : std::uint8_t {
    Buy,
    Sell,
    Repair,
    Identify,
    Count,
};

// The six situations, in the table's row order.
enum class MerchantSituation : std::uint8_t {
    NotEnoughGold,
    NoSkill,
    RegularSkill,
    GoodSkill,
    WrongType,
    Unnecessary,
    Count,
};

inline constexpr std::size_t kMerchantActionCount = static_cast<std::size_t>(MerchantAction::Count);
inline constexpr std::size_t kMerchantSituationCount =
    static_cast<std::size_t>(MerchantSituation::Count);

enum class MerchantTextError : std::uint8_t {
    None,
    // No row carries the expected "Buy" / "Sell" header.
    NoHeader,
};

class MerchantTextTable {
public:
    MerchantTextTable() = default;

    [[nodiscard]] static MerchantTextError parse(const TextTable& table, MerchantTextTable& out);

    // What to say. Empty where the table writes "n/a", which is where the
    // situation cannot arise for that action — a merchant of the wrong type
    // has nothing to say about your buying, only about your selling.
    [[nodiscard]] std::string_view line(MerchantSituation situation,
                                        MerchantAction action) const noexcept;

    [[nodiscard]] std::size_t filled() const noexcept;

private:
    std::array<std::array<std::string, kMerchantActionCount>, kMerchantSituationCount> lines_;
};

}  // namespace starhaven::data

#endif  // STARHAVEN_CORE_DATA_MERCHANT_TEXT_HPP
