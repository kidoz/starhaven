// Synthetic scripts only.
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "core/world/map_script.hpp"

using namespace starhaven;
using namespace starhaven::world;

namespace {

void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value,
         std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU);
    }
}

ScriptStep change(std::uint32_t index, std::uint8_t visible, std::string_view name) {
    ScriptStep result{1, 0, kOpcodeSetDecoration, std::vector<std::uint8_t>(5)};
    put(result.arguments, 0, index);
    result.arguments[4] = visible;
    result.arguments.insert(result.arguments.end(), name.begin(), name.end());
    result.arguments.push_back(0);
    return result;
}

}  // namespace

TEST_CASE("decoration changes validate their bounded layout and signed index", "[decorations]") {
    const auto valid = change(0x123456, 255, "Lamp");
    const auto decoded = parse_decoration_change(valid);
    if (!decoded) {
        FAIL("complete decoration change did not decode");
        return;
    }
    REQUIRE(decoded->index == 0x123456);
    REQUIRE(decoded->visible);
    REQUIRE(decoded->name == "Lamp");
    for (std::size_t n = 0; n < valid.arguments.size(); ++n) {
        auto truncated = valid;
        truncated.arguments.resize(n);
        REQUIRE_FALSE(parse_decoration_change(truncated));
    }
    REQUIRE_FALSE(
        parse_decoration_change(change(std::numeric_limits<std::uint32_t>::max(), 1, "0")));
    REQUIRE(parse_decoration_change(change(0, 0, "")));  // empty name resolves to descriptor zero
    auto other = valid;
    other.opcode = kOpcodeRetexture;
    REQUIRE_FALSE(parse_decoration_change(other));
}
